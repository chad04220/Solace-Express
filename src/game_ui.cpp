// Somnium Volandi - menus, hub screens and flight HUD
#include "aero.h"
#include "game.h"
#include "earth_charts.h"
#include "menu_layout.h"
#include "hangar_catalog.h"
#include "ui_identity.h"
#include "load_pacer.h"
#include "world_load_progress.h"
#include <ctime>

// High-tech glass UI: deep navy panels, cyan accents, amber for warnings
static const vec3 C_PANEL = svui::ink, C_PANEL2 = svui::panel, C_ACCENT = svui::brass, C_WARN = svui::amber;
static const vec3 C_TEXT = svui::text, C_DIM = svui::dim;
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
static const vec3 C_BTN(.047f,.066f,.090f), C_BTN_HI(.16f,.16f,.14f), C_INK = svui::ink;

static uint32_t uid(float x, float y, const std::string& s) {
  uint32_t h = 2166136261u;
  auto mix = [&](uint32_t v) { h ^= v; h *= 16777619u; };
  mix((uint32_t)(int)x); mix((uint32_t)(int)y);
  for (char c : s) mix((uint8_t)c);
  return h;
}
static vec3 mixc(vec3 a, vec3 b, float t) { return a + (b - a) * t; }

void Game::applyUiPalette() { applyPalette(set.cbHud); }
// (by the height, and no wider than the layouts' 1280 px at 140% allow: an 800 x 600 window at 140% clipped the
// settings off the right - the review of v3.44.0, UI-4)
float Game::S() const {
  const float byH = std::max(0.6f, g_ren.H / 720.f) * set.uiScale;
  return g_ren.W > 0 ? std::min(byH, g_ren.W / (1280.f / 1.4f)) : byH;   // (no window, headless: by the height alone)
}

// (the pointer: the mouse's, or the left stick's cursor; while the D-pad leads, the focus's place - focusNavigate)
bool Game::pointerOverOverlay() const {
  const bool fp = focusNav && focusPointer;
  const float mx = fp ? focusPointerX : in.mx, my = fp ? focusPointerY : in.my;
  return showRadio && !radioDrawing && radioRect[2] > 0.f &&
         mx >= radioRect[0] && mx < radioRect[0] + radioRect[2] && my >= radioRect[1] && my < radioRect[1] + radioRect[3];
}

bool Game::hovered(float x, float y, float w, float h) const {
  if (pointerOverOverlay()) return false;   // (beneath the radio panel: it owns the pointer there)
  const bool fp = focusNav && focusPointer;
  const float mx = fp ? focusPointerX : in.mx, my = fp ? focusPointerY : in.my;
  if (hitClipOn && (mx < hitClip[0] || mx >= hitClip[2] || my < hitClip[1] || my >= hitClip[3])) return false;
  return mx >= x && mx < x + w && my >= y && my < y + h;
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
  // Quiet instrument surfaces: typography and selection carry the hierarchy.
  float s = S(), opacity = std::min(1.f, a / 0.78f);
  g_ren.glow(x, y, w, h, vec3(0, 0, 0), 0.24f * opacity, 6 * s, 14 * s);
  g_ren.rectGrad(x, y, w, h, svui::panel, svui::ink, (uiGlass ? 0.94f : 0.98f) * opacity, 6 * s);
  g_ren.rectOutline(x, y, w, h, C_DIM, 0.16f * opacity, 6 * s, s);
  g_ren.rect(x + 16 * s, y, std::min(40 * s, w - 32 * s), s, C_ACCENT, 0.65f * opacity);
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
  float tw = g_ren.text(x, y, 12 * s, label, C_DIM, 1, 0, false);
  float lx = x + tw + 16 * s;
  if (lx < x + w) g_ren.rect(lx, y + 7 * s, x + w - lx, s, C_DIM, 0.18f);
}

// Selectable list card with animated hover / selection glow
void Game::card(float x, float y, float w, float h, bool sel, bool hov, vec3 accent) {
  float s = S();
  float k = anim(uid(x, y, "card"), sel ? 1.f : hov ? 0.45f : 0.f, 14);
  g_ren.rectGrad(x, y, w, h, mixc(vec3(0.035f, 0.059f, 0.083f), vec3(0.065f, 0.14f, 0.18f), k), mixc(C_PANEL, vec3(0.03f, 0.08f, 0.11f), k), 0.96f, 4 * s);
  g_ren.rectOutline(x, y, w, h, sel ? accent : C_DIM, 0.1f + 0.32f * k, 4 * s, s);
  if (sel) g_ren.rect(x, y + 10 * s, 3 * s, std::max(4 * s, h - 20 * s), accent, 1, s);
}

static std::string ellipsize(const std::string& str, float width, float size);
// Scrollable workspaces hash buttons in content coordinates; focus survives viewport movement.
static float buttonContentOffsetY = 0.f;
// (a label "shown##key" shows "shown" and takes its identity from the key: a toggle whose label flips between ON and
// OFF keeps its keyboard / D-pad focus - the review of v3.44.0, UI-3)
bool Game::button(float x, float y, float w, float h, const std::string& labelId, bool enabled, bool highlight) {
  float s = S(), r = 4 * s;
  const size_t cut = labelId.find("##");
  const std::string label = cut == std::string::npos ? labelId : labelId.substr(0, cut);
  uint32_t id = uid(x, std::round(y + buttonContentOffsetY), cut == std::string::npos ? labelId : labelId.substr(cut));
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
    g_ren.glow(x, y, w, h, C_ACCENT, 0.08f + 0.12f * t, r, 6 * s);
    g_ren.rectGrad(x, y, w, h, C_ACCENT * (0.9f + 0.1f * t), C_ACCENT * 0.78f, 0.96f, r);
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
  if (pr > 0.01f) g_ren.rect(x, y, w, h, vec3(1, 1, 1), 0.18f * pr, r);
  float ts = std::min(h * 0.46f, 18 * S());
  { float tw = g_ren.textWidth(label, ts), room = w - 16 * s; if (tw > room && tw > 0.f) ts = std::max(ts * room / tw, 9 * s); }   // (a long label shrinks to fit)
  g_ren.text(x + w * 0.5f, y + (h - ts) * 0.5f - ts * 0.08f + pr * 1.f * s, ts, ellipsize(label, w - 12 * s, ts), tc, 1.f, 1, false);
  if (focused) g_ren.rectOutline(x - 3 * s, y - 3 * s, w + 6 * s, h + 6 * s, C_ACCENT, 0.7f + 0.3f * sinf(realTime * 6.f), r + 2 * s, 2.f * s);
  if (focused && (in.pressed[K_ENTER] || in.pressed[' '])) { in.pressed[K_ENTER] = in.pressed[' '] = false; g_audio.triggerUI(SFX_CLICK,2*(x+w*.5f)/std::max(g_ren.W,1)-1,1-2*(y+h*.5f)/std::max(g_ren.H,1)); return true; }
  if (hov && !focused && in.mPressed[0]) { g_audio.triggerUI(SFX_CLICK,2*(x+w*.5f)/std::max(g_ren.W,1)-1,1-2*(y+h*.5f)/std::max(g_ren.H,1)); return true; }
  if (focused && in.mPressed[0] && hovered(x, y, w, h)) { g_audio.triggerUI(SFX_CLICK,2*(x+w*.5f)/std::max(g_ren.W,1)-1,1-2*(y+h*.5f)/std::max(g_ren.H,1)); return true; }
  return false;
}

// A control drawn by hand (a hub tab, a key-binding cell) in the keyboard / D-pad walk, as a button is: true when it
// has the focus (outlined); *activated when Enter or Space takes it this frame (the review of v3.44.0, UI-5: neither
// the hub's tabs nor the binding cells could be reached without a mouse)
bool Game::focusHere(uint32_t id, float x, float y, float w, float h, bool* activated) {
  const float s = S();
  focusList.push_back({id, x, y, w, h});
  const bool f = focusNav && focusId == id;
  if (f) g_ren.rectOutline(x - 3 * s, y - 3 * s, w + 6 * s, h + 6 * s, C_ACCENT, 0.7f + 0.3f * sinf(realTime * 6.f), 6 * s, 2.f * s);
  if (activated) {
    *activated = f && (in.pressed[K_ENTER] || in.pressed[' ']);
    if (*activated) { in.pressed[K_ENTER] = in.pressed[' '] = false; g_audio.triggerUI(SFX_CLICK,2*(x+w*.5f)/std::max(g_ren.W,1)-1,1-2*(y+h*.5f)/std::max(g_ren.H,1)); }
  }
  return f;
}

uint32_t Game::uiId(float x, float y, const std::string& s) { return uid(x, y, s); }
bool Game::focusAdjust(uint32_t id, float x, float y, float w, float h, int* step) {
  const float s = S();
  focusList.push_back({id, x, y, w, h, true});
  const bool f = focusNav && focusId == id;
  if (f) g_ren.rectOutline(x - 3 * s, y - 3 * s, w + 6 * s, h + 6 * s, C_ACCENT, 0.7f + 0.3f * sinf(realTime * 6.f), 6 * s, 2.f * s);
  if (step) { *step = f ? focusStep : 0; if (*step) g_audio.triggerUI(SFX_CLICK,2*(x+w*.5f)/std::max(g_ren.W,1)-1,1-2*(y+h*.5f)/std::max(g_ren.H,1)); }
  return f;
}

void Game::focusScrollPixels(size_t first, uint32_t id, float x, float top, float w, float bottom, float& scroll, bool wheeled, bool scrolls) {
  const float s = S();
  const size_t end = focusList.size();
  // (a window that scrolls: its scroll bar, at its right edge, a stop of its own - focused, the right stick scrolls it)
  if (scrolls) focusHere(id, x + w - 12 * s, top, 12 * s, bottom - top);
  if (!focusNav) return;
  for (size_t k = first; k < end; ++k) if (focusList[k].id == focusId) {
    const float fy = focusList[k].y, fh = focusList[k].h;
    if (fy >= top && fy + fh <= bottom) return;
    if (!wheeled) { if (fy < top) scroll -= top - fy + 8 * s; else scroll += fy + fh - bottom + 8 * s; return; }
    // the window moved under the focus: it goes along, to the nearest control still shown (or the scroll bar)
    uint32_t best = id; float bd = 1e18f;
    for (size_t j = first; j < end; ++j) {
      const Focusable& f = focusList[j];
      if (f.y < top || f.y + f.h > bottom) continue;
      const float d = fabsf(f.y + f.h * 0.5f - (fy + fh * 0.5f));
      if (d < bd) { bd = d; best = f.id; }
    }
    focusId = best;
    return;
  }
}

void Game::focusScrollRows(const std::function<uint32_t(int)>& idOf, int count, int& rowsFirst, int visible, int perRow, bool wheeled) {
  if (!focusNav || count <= 0) return;
  for (int i = 0; i < count; ++i) if (idOf(i) == focusId) {
    const int row = i / perRow;
    if (row >= rowsFirst && row < rowsFirst + visible) return;
    if (wheeled) {   // (scrolled away from the focus: it goes along, onto the nearest row still shown)
      const int r = row < rowsFirst ? rowsFirst : rowsFirst + visible - 1;
      focusId = idOf(std::clamp(r * perRow + i % perRow, 0, count - 1));
    } else rowsFirst = row < rowsFirst ? row : row - visible + 1;
    return;
  }
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

// A soft edge over the foot of a clipped, scrolling region: the row the clip cuts fades into the panel instead of
// ending in half glyphs against the footer under it (the hangar inspector, a job's brief, the logbook, the airline, the
// research rail). c is the panel's colour where the fade sits: panelAt for the hub's panels.
void clipFade(float x, float bottom, float w, float h, vec3 c) {
  const int n = 8;
  for (int i = 0; i < n; i++) g_ren.rect(x, bottom - h + h * i / n, w, h / n + 1, c, 0.95f * (i + 1) / n);
}
// (the hub's panels shade from svui::panel at the top to svui::ink at the foot: their colour at a height)
static vec3 panelAt(float y0, float h, float y) { const float t = clampf((y - y0) / std::max(h, 1.f), 0.f, 1.f); return svui::panel * (1 - t) + svui::ink * t; }

// A key in a key / value list: plain, or in the hub's glass style a small upper-case label
static std::string upperS(std::string t) { for (char& c : t) c = (char)toupper((unsigned char)c); return t; }
static std::string lowerS(std::string t) { for (char& c : t) c = (char)tolower((unsigned char)c); return t; }
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
// Distance-field orbit rings around the native-resolution icon, with a work-completion
// bar and separate current-task/detail lines. Decoration never changes the progress.
// fade: 1 = fully shown, falls to 0 as it hands over to the main menu.
void Game::drawIntro(float progress, const std::string& stage, float t, unsigned icon, float fade, Renderer& R, const LoadPacer* pace, bool motion) {
  const float W=float(R.W),H=float(R.H),s=std::max(.25f,std::min(W/1120.f,H/720.f));
  const float e=smoothstepf(0.f,.45f,t)*fade,mt=motion?t:0.f;
  const vec3 gold=svui::brass,ice=svui::ice;
  auto fit=[&](std::string v,float room,float fs){if(R.textWidth(v,fs)<=room)return v;while(!v.empty()&&R.textWidth(v+"...",fs)>room)v.pop_back();return v+"...";};
  R.rectGrad(0,0,W,H,svui::panel,vec3(.006f,.011f,.022f),1.f);
  for(int i=0;i<18;++i)R.rect(W*i/17.f,0,1,H,ice,.025f*e);
  for(int i=0;i<11;++i)R.rect(0,H*i/10.f,W,1,ice,.026f*e);
  R.text(60*s,34*s,11*s,"SOMNIUM VOLANDI  /  FLIGHT SYSTEMS",gold,e,0,false);
  R.text(W-60*s,34*s,11*s,fmt("INITIALIZATION  /  %d:%02d",int(t)/60,int(t)%60),C_DIM,e,2,false);
  R.rect(60*s,59*s,W-120*s,s,gold,.25f*e);
  R.text(60*s,87*s,56*s,"SOMNIUM",C_TEXT,e,0,false);
  R.text(60*s,145*s,56*s,"VOLANDI",C_TEXT,e,0,false);
  R.rect(62*s,216*s,48*s,2*s,gold,e);
  R.text(60*s,238*s,16*s,"THE DREAM OF FLIGHT",gold,.9f*e,0,false);
  R.text(60*s,265*s,12*s,"A career to build. A world to explore.",C_DIM,e,0,false);
  const float cx=W*.77f,cy=178*s,rr=104*s,isz=146*s;
  R.glow(cx-rr,cy-rr,rr*2,rr*2,ice,.08f*e,rr,48*s);
  for(int j=0;j<3;++j) {
    const float r=rr+(14*j)*s;
    R.rectOutline(cx-r,cy-r,r*2,r*2,j==1?gold:ice,(j==1?.25f:.10f)*e,r,s);
    const float ang=mt*(j==1?-.18f:.12f)+j*1.9f;
    R.rect(cx+cosf(ang)*r-2*s,cy+sinf(ang)*r-2*s,4*s,4*s,gold,e,2*s);
  }
  if(icon)R.image(icon,cx-isz*.5f,cy-isz*.5f,isz,isz,0,0,1,1,e);
  else {R.text(cx,cy-23*s,46*s,"SV",gold,e,1,false);}
  R.text(cx,cy+150*s,10*s,"CIVILIAN  /  SERVICE  /  RESEARCH",C_DIM,e,1,false);
  const float bx=60*s,by=H-306*s,bw=W-120*s,mw=bw*.58f;
  R.rectGrad(bx,by,bw,264*s,svui::panel,svui::ink,.98f*e,5*s);
  R.rectOutline(bx,by,bw,264*s,gold,.25f*e,5*s,s);
  R.rect(bx+22*s,by,40*s,2*s,gold,e);
  const float px=bx+24*s,room=mw-48*s,rail=bx+mw+22*s,rw=bw-mw-46*s;
  R.rect(bx+mw,by+22*s,s,220*s,C_DIM,.15f*e);
  LoadPacer::Snapshot work;if(pace)work=pace->snapshot();
  const float p=clampf(progress,0.f,1.f);
  const int percent=p>=1.f?100:std::min(99,int(p*100.f));
  R.text(px,by+20*s,11*s,pace?fmt("STEP %02d / %02d  //  %s",work.step,work.steps,work.label.c_str()):"PREPARING YOUR FLIGHT DECK",gold,e,0,false);
  std::string primary=stage.empty()?"Connecting flight systems":stage,detail;
  size_t split=primary.find(" | ");const size_t colon=primary.find(": ");if(colon!=std::string::npos&&(split==std::string::npos||colon<split))split=colon;
  if(split!=std::string::npos){detail=primary.substr(split+(primary[split]==':'?2:3));primary.resize(split);}
  R.text(px,by+51*s,21*s,fit(primary,room,21*s),C_TEXT,e,0,false);
  if(detail.empty())detail=p>=1.f?"All required systems are ready.":"Completed work advances the bars below.";
  R.text(px,by+83*s,12*s,fit(detail,room,12*s),C_DIM,e,0,false);
  const float taskProgress=work.taskFraction>=0?work.taskFraction:work.stageFraction;
  const std::string taskStat=!pace?"WORK IN PROGRESS":work.taskUnits>=2?fmt("%d%% / %d of %d / %.0fs",std::min(100,int(taskProgress*100)),int(work.taskDone),int(work.taskUnits),work.elapsed):fmt("%d%% / %.0fs",std::min(100,int(taskProgress*100)),work.elapsed);
  R.text(px,by+117*s,10*s,fit(work.taskLabel.empty()?"CURRENT STAGE":work.taskLabel,room-R.textWidth(taskStat,12*s)-16*s,10*s),C_DIM,e,0,false);
  R.text(px+room,by+114*s,12*s,taskStat,gold,e,2,false);
  if(pace)svui::bar(R,px,by+137*s,room,4*s,taskProgress,gold,e);
  else {R.rect(px,by+137*s,room,4*s,gold,.12f*e);R.rect(px+fmodf(mt*.16f,1.f)*room,by+136*s,3*s,6*s,gold,e);}
  R.text(px,by+163*s,10*s,"OVERALL INITIALIZATION",C_DIM,e,0,false);
  R.text(px+room,by+151*s,30*s,fmt("%d%%",percent),C_TEXT,e,2,false);
  svui::bar(R,px,by+190*s,room,7*s,p,gold,e);
  const std::string status=p>=1.f?"Ready. Welcome aboard.":pace&&work.idle>=5?fmt("Task running  /  %.0f s since its last completed unit",work.idle):"Progress measures completed work; task duration can vary.";
  R.text(px,by+215*s,11*s,fit(status,room,11*s),C_DIM,e,0,false);
  if(pace) {   // every category of the launch's work (load_pacer.h: its steps grouped by label), each with its own bar
    const int n=std::min(9,int(work.groups.size()));
    const float rowH=std::min(35.f,(264.f-26.f)/std::max(1,n))*s;
    for(int i=0;i<n;++i) {
      const auto& g=work.groups[i];const float units=g.units+g.skipped,f=units>0?clampf(g.done/units,0.f,1.f):1.f,yy=by+23*s+i*rowH;
      const vec3 c=g.skipped>0?gold:f>=1.f?ice:g.active?gold:C_DIM;
      R.rect(rail,yy+3*s,4*s,4*s,c,(g.active?.85f:.40f)*e,2*s);
      R.text(rail+12*s,yy,10*s,fit(g.label,rw-60*s,10*s),c,e,0,false);
      R.text(rail+rw,yy,10*s,g.skipped>0?"SKIPPED":f>=1.f?"READY":fmt("%d%%",std::min(99,int(f*100))),c,e,2,false);
      svui::bar(R,rail+12*s,yy+std::min(19.f*s,rowH-7*s),rw-12*s,3*s,f,c,e);
    }
  } else {
    const char* labels[]={"SHADER PROGRAMS","WORLD","PILOT SYSTEMS","RENDERER","MENU SCENERY","AIRCRAFT BODIES","PREVIEW STAGE"};
    R.text(rail,by+23*s,11*s,"SYSTEM CHECKLIST",gold,e,0,false);
    for(int i=0;i<7;++i)R.text(rail,by+(52+i*29)*s,11*s,labels[i],C_DIM,.8f*e,0,false);
  }
  R.text(60*s,H-24*s,10*s,"2026 CDAIII.  ALL RIGHTS RESERVED.",C_DIM,.6f*e,0,false);
  R.text(W-60*s,H-24*s,10*s,"SOMNIUM VOLANDI",gold,.6f*e,2,false);
}

// ------------------------------------------------------------------ pre-flight loading screen
void Game::drawLoading() {
  float W = (float)g_ren.W, H = (float)g_ren.H;
  const LoadingLayout L = loadingLayout(W, H);
  const float s = L.scale;
  auto fs = [s](float units,float minimum) { return std::max(units*s,minimum); };
  auto fitLoading = [&](std::string text, float room, float size) {
    if (g_ren.textWidth(text, size) <= room) return text;
    while (!text.empty() && g_ren.textWidth(text + "...", size) > room) text.pop_back();
    return text + "...";
  };
  // a launch's card before its flight is set up (Game::queueLaunch; Deploy's pendingMilitaryLaunch): its own flight,
  // aircraft and look, and nothing decoded from disk or rendered until it has been shown
  const bool pendingService = pendingMilitaryLaunch.has_value();
  const PendingLaunch* queued = pendingLaunch ? &*pendingLaunch : nullptr;
  const bool pending = pendingService || queued;
  const Contract& flight = pendingService ? *pendingMilitaryLaunch : queued ? queued->card : contract;
  const AircraftSpec& aircraft = pendingService ? kAircraft[flight.forceAircraft] : queued ? kAircraft[queued->spec] : plane.spec ? *plane.spec : kAircraft[specIdx];
  const Weather& weather = pending ? flight.wx : wx;
  const bool military = pendingService || (queued ? isMilitaryContract(flight.type) : militaryFlight);
  const bool research = queued ? queued->kind == LAUNCH_RESEARCH : researchFlight;
  const bool freeLook = queued ? queued->kind == LAUNCH_FREE : freeFlight;
  const vec3 accent = military ? svui::amber : research ? svui::code : C_ACCENT;
  const vec3 ink = military ? svui::field : research ? svui::codeInk : C_INK;
  const vec3 dim = military ? vec3(.69f,.73f,.64f) : research ? svui::codeDim : C_DIM;
  bool air = pending ? flight.startAirborne : !plane.onGround;
  const Airport& ap = g_world.airports[air && !flight.longHaul() ? flight.to : flight.from];
  // a picture rendered by SomniumVolandi.exe --loadshots (platform_win32.cpp) - the airport, or the aircraft in flight - when there is one on disk
  std::string key = g_world.procedural ? g_world.regionId + "/" + ap.code :
    (military || air) ? fmt("air_%d", pendingService ? flight.forceAircraft : queued ? queued->spec : typeOf(plane.spec)) : std::string(ap.code);
  auto li = loadImg.find(key);
  if (li == loadImg.end() && !pending) {
    unsigned tex = 0; int w = 0, h = 0; std::vector<uint8_t> px;
    // a picture rendered on this PC by --loadshots (.png) wins over the one that ships with the game (.jpg)
    for (const char* ext : {".png", ".jpg"})
      if (!g_world.procedural && !tex && g_ren.ok && readImage((assetDir + "/loading/" + key + ext).c_str(), w, h, px)) tex = g_ren.makeTexture(px.data(), w, h);
    li = loadImg.emplace(key, tex).first;
  }
  unsigned pic = li == loadImg.end() ? 0 : li->second;
  const auto& preview = g_ren.loadingPreview();
  const bool retained = pendingService && !pic && preview.matches(flight.forceAircraft, flight.type-CT_MIL_RECON);
  if (retained) pic = preview.texture;
  if (!military && !loadMap && !pic && !pending && g_ren.ok) {   // otherwise the aerial image of the airport, rendered once from the real terrain
    loadMap = true; gpsMapValid = false;
    g_ren.renderMap(ap.x, ap.z, 2400.f, 1536);
  }
  // (the backdrop in hand - the picture decoded, or the map rendered: the screen can now be drawn again from inside a
  // bake, which must neither decode nor render)
  if (!pending) loadBackdrop = true;
  float rt = loadReadyT >= 0 ? loadT - loadReadyT : 0.f;
  float cover = 1.f - smoothstepf(0.f, 1.4f, rt);   // the card fades out onto the live shot once the scenery is in
  if (cover > 0.001f && (!military || pic || pending)) {
    g_ren.rect(0, 0, W, H, vec3(0.01f, 0.02f, 0.035f), cover);
    if (pic) {   // slow push-in over the picture (cropped to fill the screen)
      const float imageAspect = retained ? float(preview.width) / preview.height : 16.f / 9.f;
      float z = 1.f - 0.06f * (set.menuMotion ? std::min(loadT, 12.f) / 12.f : 0.f), asp = (W / H) / imageAspect;
      float su = z * std::min(1.f, asp), sv = z * std::min(1.f, 1.f / asp);
      // Framebuffer copies start at GL's bottom edge; packaged pictures use top-to-bottom image rows.
      const float v0 = retained ? .5f + sv*.5f : .5f - sv*.5f, v1 = 1.f-v0;
      g_ren.image(pic, 0, 0, W, H, 0.5f - su * 0.5f, v0, 0.5f + su * 0.5f, v1, cover);
      g_ren.flushUIPublic();
    } else if (!military && g_ren.mapTex()) {   // slow push-in over the aerial image
      float z = 0.62f - 0.05f * (set.menuMotion ? std::min(loadT, 12.f) / 12.f : 0.f);
      float sv = z, su = z * W / H;
      if (su > 0.98f) { sv *= 0.98f / su; su = 0.98f; }
      g_ren.image(g_ren.mapTex(), 0, 0, W, H, 0.5f - su * 0.5f, 0.5f - sv * 0.5f, 0.5f + su * 0.5f, 0.5f + sv * 0.5f, cover);
      g_ren.flushUIPublic();
    }
    // runway marker on the image
  }
  // Confine legibility shading to the header and telemetry strip; the aircraft
  // in the middle of the photograph stays unobstructed and keeps its contrast.
  g_ren.rectGrad(0, L.y-18*s, W, H-L.y+18*s, vec3(0, 0, 0), ink, 0.40f);
  g_ren.rectGrad(0, 0, W, 90 * s, vec3(0.0f, 0.01f, 0.02f), vec3(0, 0, 0), 0.55f);
  if(research)svui::matrix(g_ren,0,0,W,H,realTime,.18f,set.menuMotion,W*.55f,(L.artTop+L.artBottom)*.5f,W*.32f,(L.artBottom-L.artTop)*.45f);
  if(military)svui::chevrons(g_ren,40*s,13*s,180*s,s,accent,.55f);
  g_ren.text(40*s,26*s,fs(12,11),"SOMNIUM VOLANDI",accent,1,0,false);
  g_ren.text(W-40*s,26*s,fs(11,9),military?"DEFENSE COMMAND / SORTIE PREPARATION":research?"NIGHTGLASS / FLIGHT INITIALIZATION":freeLook?"FREE FLIGHT / DEPARTURE PREPARATION":flight.type==CT_LESSON?"FLIGHT SCHOOL / PREPARATION":"FLIGHT OPERATIONS / PREPARATION",dim,1,2,false);
  g_ren.text(40 * s, 50 * s, 30 * s, fitLoading(flight.title, W - 80 * s, 30 * s), C_TEXT, 1);
  // Mission details and four real readiness bars fit into a shallow bottom strip.
  const float cx=L.x+18*s, cy=L.y, cw=L.width-36*s;
  if(military)svui::fieldSurface(g_ren,L.x,cy,L.width,L.height,s);
  else {
    g_ren.rectGrad(L.x,cy,L.width,L.height,research?svui::codePanel:svui::panel,ink,.97f,5*s);
    g_ren.rectOutline(L.x,cy,L.width,L.height,accent,.28f,5*s,s);
  }
  const Airport& from = g_world.airports[flight.from];
  const Airport& to = arrivalAirport(flight);   // (a long haul's: in its own region)
  const float aircraftX=cx+cw*.32f, weatherX=cx+cw*.60f;
  g_ren.text(cx,cy+12*s,fs(10,9),air?"IN FLIGHT / INBOUND":"DEPARTURE",dim,1,0,false);
  g_ren.text(cx,cy+29*s,fs(19,13),fitLoading(std::string(ap.code)+"  "+ap.name,cw*.32f-18*s,fs(19,13)),C_TEXT,1);
  g_ren.text(cx,cy+53*s,fs(10,9),military?"Equipment and fuel included":flight.longHaul()?fmt("LONG HAUL  %s > %s  %.0f km",from.code,to.code,flight.haulKm):flight.from!=flight.to?fmt("ROUTE  %s > %s",from.code,to.code):"Local flight / same departure and arrival",dim,1,0,false);
  g_ren.text(aircraftX,cy+12*s,fs(10,9),military?"SERVICE AIRCRAFT":"AIRCRAFT",dim,1,0,false);
  g_ren.text(aircraftX,cy+29*s,fs(16,12),fitLoading(aircraft.name,cw*.28f-18*s,fs(16,12)),C_TEXT,1);
  if(military&&flight.from!=flight.to)g_ren.text(aircraftX,cy+53*s,fs(10,9),fmt("ROUTE  %s > %s",from.code,to.code),dim,1,0,false);
  g_ren.text(weatherX,cy+12*s,fs(10,9),"CONDITIONS",dim,1,0,false);
  g_ren.text(weatherX,cy+29*s,fs(12,10),fitLoading(weather.describe(),cw*.40f,fs(12,10)),C_TEXT,1);
  // Separate measured stage bars reveal the work behind the overall percentage.
  const float scene=loadSetupDone();
  const float chunks=loadPend0>0?clampf(1.f-float(std::max(0,g_ren.entPending))/loadPend0,0.f,1.f):scene;
  const float lighting=loadFrames>=2&&!loadingShadowPending()?1.f:0.f;
  const float checks=loadReadyT>=0?1.f:clampf(loadStableFrames/2.f,0.f,1.f);
  const float fractions[]={scene,chunks,lighting,checks};
  const char* stages[]={"SCENE SETUP","NEARBY SCENERY","TERRAIN LIGHTING","READINESS CHECK"};
  for(int i=0;i<4;++i){const float xx=cx+cw*i/4,ww=cw/4-18*s,yy=cy+75*s;
    g_ren.text(xx,yy,fs(9,8),stages[i],dim,1,0,false);
    g_ren.text(xx,yy+16*s,fs(10,9),fractions[i]>=1?"READY":pending?"WAITING":i==1?fmt("%d remaining",std::max(0,g_ren.entPending)):i==3?fmt("%d / 2 stable frames",std::min(2,loadStableFrames)):"IN PROGRESS",fractions[i]>=1?accent:C_TEXT,1,0,false);
    svui::bar(g_ren,xx,yy+34*s,ww,3*s,fractions[i],accent);
  }
  // progress / ready prompt
  float by = cy+139*s, bw = cw, bh = 6*s;
  if (loadReadyT < 0) {
    const int warmN = headless || pending ? 0 : loadWarmItems(), hiveN = warmN - (specIdx == kSomnus ? 1 : 0);
    const std::string step = loadWarmStep < warmN ? (loadWarmStep < hiveN ? fmt("01 / 04  Preparing the hive craft  %d / %d", loadWarmStep + 1, hiveN) : std::string("01 / 04  Preparing the stores")) :
      loadFrames < 2 ? "01 / 04  Preparing flight scene" : !loadViewsDrawn && !headless ? ((loadViewsStep == 0) == (camMode == 1) ? "01 / 04  Preparing the cockpit view" : "01 / 04  Preparing the outside view") : g_ren.entPending > 0 ? "02 / 04  Loading nearby scenery" : loadingShadowPending() ? "03 / 04  Preparing terrain shadows" : "04 / 04  Checking flight readiness";
    g_ren.text(cx, by - 19 * s, fs(12,10), step, C_TEXT, 0.9f, 0, false);
    g_ren.text(cx + bw, by - 19 * s, fs(12,10), fmt("%d%%", std::min(99, (int)(clampf(loadShown, 0.f, 1.f) * 100.f))), accent, 1, 2, false);
    g_ren.rect(cx, by, bw, bh, accent, 0.12f, 2 * s);
    if (loadShown > 0.f) g_ren.rectGrad(cx, by, bw * loadShown, bh, accent * 0.7f, accent, 1, 2 * s);
    const std::string detail=pendingService?"Preparing service assignment and supplied aircraft":queued?"Preparing the flight":fmt("Elapsed %.1f s  /  %d scene frames  /  ",loadT,loadFrames)+(g_ren.entPending>0?fmt("%d scenery chunks remaining",g_ren.entPending):loadingShadowPending()?"Terrain shadow pass running":"Verifying two stable rendered frames");
    g_ren.text(cx, by + 13 * s, fs(10,9), fitLoading(detail,bw,fs(10,9)),dim,.9f,0,false);
    float hx = cx + bw * loadShown;
    g_ren.glow(hx - 4 * s, by - 2 * s, 8 * s, bh + 4 * s, accent, 0.5f + 0.3f * sinf(realTime * 6.f), 4 * s, 14 * s);
  } else {
    float pulse = 0.6f + 0.4f * sinf(realTime * 3.f);
    float bx = cx, bwid = 340 * s;
    g_ren.glow(bx, by - 20 * s, bwid, 40 * s, accent, 0.25f * pulse, 4 * s, 16 * s);
    g_ren.rectGrad(bx, by - 20 * s, bwid, 40 * s, accent * 1.05f, accent * 0.6f, 0.95f, 4 * s);
    g_ren.text(bx + bwid * 0.5f, by - 9 * s, fs(16,12), padPrompts() ? "PRESS  " + padGlyph("A") + "  TO FLY" : std::string("CLICK OR PRESS ENTER TO FLY"), ink, 1, 1, false);
    g_ren.text(bx + bwid + 20 * s, by - 6 * s, fs(12,10), padPrompts() ? padGlyph("B") + "  BACK" : std::string("ESC  BACK"), dim, 0.9f, 0, false);
    g_ren.text(cx+cw,by-6*s,fs(12,10),"100% / FLIGHT READY",accent,1,2,false);
  }
  {   // a pilot's tip while it loads: the next one each flight
    static const char* tips[] = {
      "Two white and two red PAPI lights by the runway: you're on a 3 degree glidepath.",
      "Butter landings - under 150 fpm - pay a bonus. Flare as the runway fills the windscreen.",
      "After landing, taxi clear, hold I to shut down, then set the parking brake (B) for the airmanship bonus.",
      "High and hot fields need more runway: Summit Pass, at 5,400 ft, takes a STOL aircraft.",
      "The wind dial shows the crosswind on your nose: amber means strong. Crab in, then kick straight.",
      "Time Warp (T) goes up to x16, and steps back down by itself near the ground, a checkpoint or trouble.",
      "Passengers sometimes ask to see a landmark on the way. Fly by it, low, for a tip.",
      "Photo mode (F2, or the pause menu) freezes the flight: frame the shot, then press Space.",
      "Every landmark you find goes in your Passport: Logbook > Passport.",
      "Trials keep a ghost of your best run. Beat it for gold - and a place on the worldwide board on Steam.",
      "Let go of the controls with assists on and the aircraft holds its bank and its climb.",
      "Check the range on the engine panel before a long crossing. Fuel is weight: carry what you need.",
      "The GPS map (N) can pick an airport for the autopilot to land at.",
      "Rough air? Settings > Flying > Air: Calm takes the edge off the gusts while you learn.",
    };
    const int n = (int)(sizeof tips / sizeof tips[0]);
    static int tip = -1; static float lastT = 1e9f;
    if (loadT < lastT) tip = (tip + 1) % n;   // (a new load: its elapsed time starts again)
    lastT = loadT;
    const float ty = cy - 44 * s, a = clampf(loadT / 0.4f, 0.f, 1.f);   // (just above the flight's panel, over the picture)
    const std::string t = fitLoading(tips[tip], cw - 90 * s, fs(13,11));
    g_ren.rect(cx - 18 * s, ty - 9 * s, g_ren.textWidth(t, fs(13,11)) + 86 * s, 32 * s, ink, 0.72f * a, 4 * s);
    g_ren.text(cx, ty, fs(11,10), "TIP", accent, a, 0, false);
    g_ren.text(cx + 40 * s, ty - 1 * s, fs(13,11), t, C_TEXT, 0.92f * a, 0, false);
  }
}

// ------------------------------------------------------------------ main menu
void Game::enterCareerHub() { screen = SCR_HUB; hubTab = TAB_CONTRACTS; hubList = 0; selContract = 0; selAircraft = -1; }
void Game::drawMenu() {
  // the first start: the welcome over the menu, the menu under it without the clicks, the keys or the pad
  const bool welcome = !profile.welcomed && !headless && !diskless;   // (never in a tool's session: its pictures show the menu)
  const bool overlay = welcome || showCredits;   // (the credits the same way: over the menu, the menu taking nothing)
  Input held;
  if (overlay) { held = in; memset(in.pressed, 0, sizeof(in.pressed)); for (bool& m : in.mPressed) m = false; in.buttonsPressed = 0; in.wheel = 0; in.text.clear(); }
  struct WelcomeAfter { Game* g; bool on, welcome; Input& held; ~WelcomeAfter() { if (!on) return; g->in = held; g_ren.flushUIPublic(); if (welcome) g->drawWelcome(); else g->drawCredits(); } } after{this, overlay, welcome, held};
  float W = (float)g_ren.W, H = (float)g_ren.H;
  float s = std::min(S(), std::min(H / 690.f, W / 1040.f));
  const float x = 48 * s, cw = std::min(370 * s, W * 0.43f), bw = cw - 48 * s;
  g_ren.rectGrad(0, 0, x + cw + 40 * s, H, vec3(0.006f, 0.018f, 0.035f), vec3(0.012f, 0.032f, 0.052f), 0.96f);
  g_ren.rect(x + cw + 40 * s, 0, s, H, C_DIM, 0.16f);
  g_ren.text(x, 42 * s, 12 * s, "SOMNIUM VOLANDI / FLIGHT OPERATIONS", C_ACCENT, 1, 0, false);
  g_ren.text(x, 78 * s, 48 * s, "SOMNIUM", C_TEXT, 1);
  g_ren.text(x, 132 * s, 48 * s, "VOLANDI", C_TEXT, 1);
  g_ren.rect(x, 200 * s, 46 * s, 3 * s, C_ACCENT, 1);
  g_ren.text(x, 220 * s, 16 * s, demo ? "The dream of flight.   DEMO" : "The dream of flight.", C_DIM, 1);
  float y = 270 * s;
  panel(x, y, cw, (hasSave ? 114 : 86) * s);
  g_ren.text(x + 24 * s, y + 16 * s, 11 * s, hasSave ? (profile.name.empty() ? std::string("CAREER READY") : ellipsize("WELCOME BACK, CAPTAIN " + [&] { std::string u; for (char ch : profile.name) u += (char)toupper((unsigned char)ch); return u; }(), bw, 11 * s)) : std::string("WELCOME ABOARD"), C_ACCENT, 1, 0, false);
  if (hasSave) {
    // (where the career is: its field's code when its world is loaded, else its region)
    const bool loaded = g_world.regionId == career.region && career.location < (int)g_world.airports.size();
    const std::string at = loaded ? std::string(g_world.airports[career.location].code) : careerPlaceName();
    g_ren.text(x + 24 * s, y + 36 * s, 15 * s, ellipsize(std::string(licenseName(career.license)) + " / " + at, bw, 15 * s), C_TEXT, 1);
    if (button(x + 24 * s, y + 64 * s, bw, 34 * s, "Continue career", true, true)) { enterCareerHub(); career.refreshBoard(); }
  } else g_ren.text(x + 24 * s, y + 40 * s, 15 * s, "Start small. Build your airline.", C_TEXT, 1);
  y += (hasSave ? 130 : 102) * s;
  if (in.pressed[K_ESC]) { confirmNew = false; confirmRes = false; }
  if (!confirmNew) {
    if (button(x, y, cw, 42 * s, "New career", true, !hasSave)) { if (hasSave) confirmNew = true; else { pendingCareer.reset(); career.newGame(); saveGame(); enterCareerHub(); } }
  } else {
    g_ren.text(x, y - 14 * s, 11 * s, "Replace your saved career?", C_WARN, 1);
    if (button(x, y, (cw - 10 * s) * 0.5f, 42 * s, "Overwrite save", true, true)) { pendingCareer.reset(); career.newGame(); saveGame(); confirmNew = false; enterCareerHub(); }
    if (button(x + (cw + 10 * s) * 0.5f, y, (cw - 10 * s) * 0.5f, 42 * s, "Cancel")) confirmNew = false;
  }
  y += 54 * s;
  if (button(x, y, cw, 42 * s, "Free flight")) { confirmNew = false; beginFreeFlightSetup(); }
  y += 54 * s;
  if (button(x, y, (cw - 12 * s) * 0.5f, 40 * s, "Settings")) { screen = SCR_HUB; hubTab = TAB_SETTINGS; settingsPage = 0; }
  if (button(x + (cw + 12 * s) * 0.5f, y, (cw - 12 * s) * 0.5f, 40 * s, "Controls")) { screen = SCR_HUB; hubTab = TAB_SETTINGS; settingsPage = 1; }
  y += 52 * s;
  // the research terminal: hidden until the career has earned it (the ATP or the campaign), or it has been found
  if (!demo && (career.license >= LIC_ATP || career.finished || profile.has("classified"))) {
    if (button(x, y, cw, 36 * s, "Project NIGHTGLASS")) { screen = SCR_RESEARCH; resOpened = realTime; g_audio.trigger(SFX_BEEP); award("classified"); }
    y += 46 * s;
  }
  if (button(x, y, (cw - 12 * s) * 0.5f, 36 * s, "Credits")) { showCredits = true; creditsByHand = false; creditsScroll = 0.f; }
  if (button(x + (cw + 12 * s) * 0.5f, y, (cw - 12 * s) * 0.5f, 36 * s, "Quit to desktop")) quit = true;
  g_ren.text(x, H - 34 * s, 11 * s, "SOMNIUM VOLANDI " + version + " / CDAIII", C_DIM, 0.8f, 0, false);   // (the version a report names)
  const float fx = x + cw + 76 * s;
  if (W - fx > 220 * s) {
    g_ren.text(fx, H - 90 * s, 11 * s, "THE DREAM OF FLIGHT", C_ACCENT, 1, 0, false);
    g_ren.text(fx, H - 67 * s, 23 * s, "Find your next horizon.", C_TEXT, 1);
  }
  if (!crashReport.empty()) {   // the last session crashed: where its report is, until dismissed
    const float nx = W - fx > 300 * s ? fx : x, nw = std::min(520 * s, W - nx - 32 * s), ny = W - fx > 300 * s ? 42 * s : H - 200 * s;
    const std::string dir = crashReport.substr(0, crashReport.find_last_of("\\/"));
    const auto lines = wrap("A crash report was saved to " + dir + ". Please send it with a note of what you were doing when "
                            "it happened. Nothing is sent automatically.", nw - 48 * s, 13 * s);
    const float nh = (86 + 18 * lines.size()) * s;
    panel(nx, ny, nw, nh);
    g_ren.text(nx + 24 * s, ny + 16 * s, 11 * s, "THE LAST SESSION CLOSED UNEXPECTEDLY", C_WARN, 1, 0, false);
    float ly = ny + 36 * s;
    for (const std::string& l : lines) { g_ren.text(nx + 24 * s, ly, 13 * s, l, C_TEXT, 1); ly += 18 * s; }
    const float bw2 = (nw - 58 * s) * 0.5f;
    if (platformOpen && button(nx + 24 * s, ly + 6 * s, bw2, 32 * s, "Open folder")) platformOpen(dir);
    if (button(nx + nw - 24 * s - bw2, ly + 6 * s, bw2, 32 * s, "Dismiss")) crashReport.clear();
  }
}

// ------------------------------------------------------------------ hub
void Game::drawHub() {
  // a card over the hub (a new licence, the campaign's end) takes the clicks, the keys and the pad: the hub under it
  // is drawn without them
  const bool card = licenceShown >= 0 || finaleShown || trialResult.kind >= 0 || demoOver();
  Input held;
  if (card) { held = in; memset(in.pressed, 0, sizeof(in.pressed)); for (bool& m : in.mPressed) m = false; in.buttonsPressed = 0; in.wheel = 0; }
  struct CardAfter { Game* g; bool on; Input& held; ~CardAfter() { if (!on) return; g->in = held; g->uiGlass = false; g_ren.flushUIPublic(); if (g->licenceShown >= 0) g->drawLicenceCard(); else if (g->finaleShown) g->drawFinaleCard(); else if (g->trialResult.kind >= 0) g->drawTrialCard(); else if (g->demoOver()) g->drawDemoEnd(); } } after{this, card, held};
  if (hubTab == TAB_CONTRACTS && hubList == 2) { FrameParams fp{}; drawMilitary(fp); return; }
  float s = S(), W = (float)g_ren.W, H = (float)g_ren.H;
  uiGlass = true;   // (panels and cards in the hub's glass style; reset at the end)
  // A steady backdrop keeps small data legible without obscuring the hangar exhibit.
  g_ren.rect(0, 0, W, H, vec3(0.004f, 0.012f, 0.024f), hubTab == TAB_HANGAR ? 0.08f : 0.48f);
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
  float tw = g_ren.text(hx, 13 * s, 24 * s, "SOMNIUM VOLANDI", C_TEXT, 1);
  g_ren.rect(hx + 2 * s, 44 * s, 54 * s, 2 * s, C_ACCENT, 1);
  g_ren.rect(hx + 60 * s, 44.5f * s, std::max(tw - 60 * s, 20 * s), 1 * s, C_ACCENT, 0.35f);
  g_ren.text(hx + 2 * s, 50 * s, 12 * s, std::string("CAREER  //  ") + licenseName(career.license), C_ACCENT, 1, 0, false);
  const bool loaded = g_world.regionId == career.region && career.location < (int)g_world.airports.size();   // (the settings, opened from the menu, may stand in another world)
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
  {   // the field, and the region when it isn't the islands
    std::string label = "LOCATION";
    if (!career.atHome()) if (auto d = procedural::find(career.region)) label += "  /  " + upperS(d->name);
    chip(label.c_str(), loaded ? fmt("%s  %s", g_world.airports[career.location].code, g_world.airports[career.location].name) : careerPlaceName(), C_TEXT, std::max(140 * s, W * 0.26f));
  }
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
    bool take = false;
    const bool foc = focusHere(0x7AB000u + (uint32_t)i, tx, ty, w, 34 * s, &take);
    bool hov = hovered(tx, ty, w, 34 * s) || foc;
    float h = anim(uid(tx, ty, "tab"), hov ? 1.f : 0.f, 14);
    if (h > 0.01f) g_ren.rectGrad(tx, ty, w, 34 * s, vec3(0.03f, 0.1f, 0.15f), vec3(0.01f, 0.04f, 0.07f), 0.5f * h, 2 * s);
    vec3 tc = hubTab == i ? C_TEXT : mixc(C_DIM, C_TEXT, h);
    const float tyt = ty + 17 * s - tfs * 0.53f;
    if (numbered) g_ren.text(tx + 14 * s, tyt, tfs, fmt("%02d", i + 1), hubTab == i ? C_ACCENT : C_ACCENT * 0.55f, 1, 0, false);
    g_ren.text(tx + 14 * s + (numbered ? g_ren.textWidth("00  ", tfs) : 0.f), tyt, tfs, tabs[i], tc, 1, 0, false);
    if (((hov && in.mPressed[0]) || take) && hubTab != i) { hubTab = i; g_audio.trigger(SFX_CLICK); }
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
    default: { float pw = std::min(cw, (settingsPage >= 1 ? 940 : 760) * s), sx = cx + (cw - pw) * 0.5f; panel(sx, cy, pw, ch); drawSettings(sx + 24 * s, cy + 20 * s, pw - 48 * s, ch - 40 * s); break; }
  }
  if (showRadio) drawRadioPanel(W - 460 * s, 126 * s);
  uiGlass = false;
  if (in.pressed[K_ESC]) { if (showRadio) showRadio = false; else { screen = SCR_MENU; if (retryCommit()) saveGame(); } }
}

void Game::drawMapView(float x, float y, float w, float h, int from, int to, const std::vector<Waypoint>* wps) {
  float s = S();
  float sz = std::min(w, h);
  float ox = x + (w - sz) * 0.5f, oy = y + (h - sz) * 0.5f;
  const float iu = (WRAP_HALF - WORLD_HALF) / WRAP_SPAN;   // (the islands' square of the map's period)
  g_ren.image(g_ren.minimapTex, ox, oy, sz, sz, iu, iu, 1.f - iu, 1.f - iu);
  g_ren.flushUIPublic();
  auto toS = [&](float wx_, float wz) { return vec2(ox + (wx_ + WORLD_HALF) / (2 * WORLD_HALF) * sz, oy + (wz + WORLD_HALF) / (2 * WORLD_HALF) * sz); };
  if (from >= 0 && to >= 0) {
    vec2 prev = toS(g_world.airports[from].x, g_world.airports[from].z);
    if (wps) for (auto& wp : *wps) { vec2 p = toS(wp.x, wp.z); g_ren.line(prev.x, prev.y, p.x, p.y, 3 * s, C_ACCENT, 0.9f); g_ren.rect(p.x - 4 * s, p.y - 4 * s, 8 * s, 8 * s, vec3(0.3f, 1, 0.5f), 1, 4 * s); prev = p; }
    vec2 b = toS(g_world.airports[to].x, g_world.airports[to].z);
    g_ren.line(prev.x, prev.y, b.x, b.y, 3 * s, C_ACCENT, 0.9f);
  }
  for (const Site& st : profileSites()) {   // the landmarks, small diamonds: gold once found (the passport has them all)
    const vec2 p = toS(st.pos.x, st.pos.z);
    if (p.x < ox || p.y < oy || p.x > ox + sz || p.y > oy + sz) continue;
    const bool got = profile.sites.count(st.id) > 0;
    const vec3 c = got ? vec3(1.f, 0.82f, 0.35f) : vec3(0.75f);
    const float r = 3.6f * s, al = got ? 1.f : 0.75f;
    g_ren.line(p.x - r, p.y, p.x, p.y - r, 2.2f * s, c, al); g_ren.line(p.x, p.y - r, p.x + r, p.y, 2.2f * s, c, al);
    g_ren.line(p.x + r, p.y, p.x, p.y + r, 2.2f * s, c, al); g_ren.line(p.x, p.y + r, p.x - r, p.y, 2.2f * s, c, al);
    if (sz > 560 * s) g_ren.text(p.x + r + 3 * s, p.y - 7 * s, 11 * s, st.name, got ? c : C_DIM, al);
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
  if(hubList==2) { FrameParams fp{}; drawMilitary(fp); return; }
  float lw = std::min(w * 0.29f, 370 * s);
  panel(x, y, lw, h);
  // build card list: story, free flight, freelance
  struct Card { const Contract* c; bool story; bool free; bool job = false; int trial = -1; bool legend = false; bool travel = false; };
  std::vector<Card> cards;
  static Contract jobCont;   // the open job's next leg (pinned on top while the job waits for it)
  const bool openJob = career.job && career.job->state == Career::JobState::RECOVERY;
  if (hubList == 1) { for (int k = 0; k < TR_COUNT; k++) cards.push_back({nullptr, false, false, false, k}); }
  else {
    if (openJob) { jobCont = career.job->continuation(); cards.push_back({&jobCont, false, false, true}); }
    const Contract* st = career.atHome() ? career.nextStory() : nullptr;   // (the campaign is flown in the islands)
    if (st) cards.push_back({st, true, false});
    cards.push_back({nullptr, false, true});
    if (career.license >= LIC_PPL || !career.atHome()) cards.push_back({nullptr, false, false, false, -1, false, true});   // the airline to another region
    static const std::vector<Contract> legends = Career::legendaryContracts();   // (after the campaign: the hardest flying in the islands)
    if (career.finished && career.atHome()) for (auto& c : legends) cards.push_back({&c, false, false, false, -1, true});
    for (auto& c : career.board) cards.push_back({&c, false, false});
  }
  selContract = std::clamp(selContract, 0, (int)cards.size() - 1);
  float cy = y + 14 * s;
  {   // the list switch: the work, or the trials (off the books, scored on a local board)
    float sw = (lw - 48 * s) / 3.f;
    // (one style for the three: the selected one lit - "WORK", "trials" and "Military" were cased three ways)
    if (button(x + 16 * s, cy - 4 * s, sw, 26 * s, "WORK", true, hubList == 0) && hubList != 0) { hubList = 0; selContract = 0; selAircraft = -1; }
    // (the trials' courses and Defense Command are the islands': away from them, the buttons say so)
    if (button(x + 24 * s + sw, cy - 4 * s, sw, 26 * s, "TRIALS", true, hubList == 1) && hubList != 1) {
      if (career.atHome()) { hubList = 1; selContract = 0; selAircraft = -1; } else { hubMsg = "The trials are flown in the Solace Islands: travel home to fly them."; hubMsgTime = 5; }
    }
    if(button(x+32*s+sw*2,cy-4*s,sw,26*s,"MILITARY")) {
      if (career.atHome()) { hubList=2; selContract=0; return; }
      hubMsg = "Defense Command is in the Solace Islands: travel home for its board."; hubMsgTime = 5;
    }
    cy += 32 * s;
  }
  if (hubList == 0) { header(x + 16 * s, cy, lw - 32 * s, ellipsize(career.finished ? "CAMPAIGN COMPLETE - FREELANCE JOBS CONTINUE" : "AVAILABLE WORK", lw - 60 * s, 13 * s)); cy += 28 * s; }
  else { header(x + 16 * s, cy, lw - 32 * s, "TRIALS  -  OFF THE BOOKS, LOCAL BEST TIMES"); cy += 28 * s; }
  // the cards scroll (mouse wheel, or the right stick) when the list is longer than the panel: every job stays
  // reachable (the review of v3.31.0, U2: the last two freelance jobs had no way in), the selected card kept in view
  const float chh = 82 * s, step = chh + 8 * s, listTop = cy, listBot = y + h - 8 * s;
  const int visCards = std::max(1, (int)floorf((listBot - listTop + 8 * s) / step)), nCards = (int)cards.size();
  static int listScroll = 0; static int listFor = -1, selSeen = -1;
  if (listFor != hubList) { listFor = hubList; listScroll = 0; }
  bool cardsWheeled = false;
  if (hovered(x, listTop, lw, listBot - listTop) && in.wheel != 0) { listScroll -= (int)in.wheel; in.wheel = 0; cardsWheeled = true; }
  if (selSeen != selContract) {   // a new selection (keys, a stick) scrolls itself into view
    selSeen = selContract;
    if (selContract < listScroll) listScroll = selContract;
    if (selContract >= listScroll + visCards) listScroll = selContract - visCards + 1;
  }
  // the D-pad walks the cards (A takes one): stepping off the window's edge scrolls the list a card (the card just
  // past each edge is in the walk), the right stick scrolling it takes the focus along
  auto cardId = [&](int i) { return uid(0, (float)(i + 1000 * hubList), "contract-card"); };
  focusScrollRows(cardId, nCards, listScroll, visCards, 1, cardsWheeled);
  listScroll = std::clamp(listScroll, 0, std::max(0, nCards - visCards));
  for (int i : {listScroll - 1, listScroll + visCards}) if (i >= 0 && i < nCards) focusList.push_back({cardId(i), x + 10 * s, cy + (i - listScroll) * step, lw - 20 * s, chh});
  if (listScroll > 0) g_ren.text(x + lw - 18 * s, listTop - 24 * s, 12 * s, fmt("^ %d more", listScroll), C_DIM, 1, 2);
  if (listScroll + visCards < nCards) g_ren.text(x + lw - 18 * s, listBot - 6 * s, 12 * s, fmt("%d more v", nCards - listScroll - visCards), C_DIM, 1, 2);
  for (int i = listScroll; i < nCards && i < listScroll + visCards; i++) {
    bool sel = i == selContract;
    bool hov = hovered(x + 10 * s, cy, lw - 20 * s, chh);
    card(x + 10 * s, cy, lw - 20 * s, chh, sel, hov, cards[i].job ? C_GOOD : cards[i].story ? C_WARN : cards[i].legend ? vec3(1.f, 0.82f, 0.35f) : C_ACCENT);
    bool take = false; focusHere(cardId(i), x + 10 * s, cy, lw - 20 * s, chh, &take);
    if ((hov && !focusNav && in.mPressed[0]) || take) { selContract = i; selAircraft = -1; launchFuelKg = -1; if (!take) g_audio.trigger(SFX_CLICK); }
    if (cards[i].trial >= 0) {
      int k = cards[i].trial;
      auto it = trialBest.find(trialKey(k));
      fitText(x + 24 * s, cy + 9 * s, lw - 48 * s, 18 * s, 13 * s, std::string("TRIAL  ") + trialName(k), C_TEXT);
      const int medal = (int)profile.count(std::string("medal.") + trialId(k));   // (the best medal won here, any aircraft)
      const float mw = medal > 0 ? 26 * s : 0.f;
      g_ren.text(x + 24 * s, cy + 36 * s, 14 * s, ellipsize(it != trialBest.end() && !it->second.empty() ? "Best: " + trialScore(k, it->second[0]) : "No time set yet", lw - 48 * s - mw, 14 * s), it != trialBest.end() ? C_GOOD : C_DIM, 1);
      if (medal > 0) {
        const vec3 mc = medal == 3 ? vec3(1.f, 0.82f, 0.3f) : medal == 2 ? vec3(0.8f, 0.85f, 0.9f) : vec3(0.85f, 0.55f, 0.3f);
        const float mx = x + lw - 40 * s, my = cy + 44 * s;
        g_ren.rect(mx - 9 * s, my - 9 * s, 18 * s, 18 * s, mc, 1, 9 * s);
        g_ren.rect(mx - 5 * s, my - 5 * s, 10 * s, 10 * s, mc * 0.55f, 1, 5 * s);
      }
    } else if (cards[i].free) {
      fitText(x + 24 * s, cy + 9 * s, lw - 48 * s, 18 * s, 13 * s, "Free Flight / Ferry", C_TEXT);
      g_ren.text(x + 24 * s, cy + 36 * s, 14 * s, ellipsize("Fly anywhere for fun or to reposition. No pay.", lw - 48 * s, 14 * s), C_DIM, 1);
    } else if (cards[i].travel) {
      fitText(x + 24 * s, cy + 9 * s, lw - 48 * s, 18 * s, 13 * s, career.atHome() ? "Travel / Beyond the islands" : "Travel / On, or home", C_TEXT);
      g_ren.text(x + 24 * s, cy + 36 * s, 14 * s, ellipsize(career.atHome() ? "Take the airline to another region and work there." : "Fly on to another region, or home to the Solace Islands.", lw - 48 * s, 14 * s), C_DIM, 1);
    } else {
      const Contract& c = *cards[i].c;
      const bool legendDone = cards[i].legend && profile.count("legend." + c.id) > 0.f;
      std::string tag = cards[i].job ? fmt("CONTINUING JOB / LEG %d", career.job->legs + 1) : cards[i].story ? fmt("CAREER / CHAPTER %02d", c.chapter + 1) : cards[i].legend ? (legendDone ? "LEGENDARY CONTRACT / FLOWN" : "LEGENDARY CONTRACT") : c.crossCountry() ? "CROSS-COUNTRY / BEYOND THE CHART" : c.longHaul() ? (earthCharts::isEarthId(c.toRegion) ? "LONG HAUL" : "LONG HAUL / ACROSS THE OCEAN") : "FREELANCE CONTRACT";
      float cardW = lw - 48 * s;
      g_ren.text(x + 24 * s, cy + 9 * s, 10 * s, tag, cards[i].legend ? vec3(1.f, 0.82f, 0.35f) : cards[i].story ? C_WARN : C_DIM, 1, 0, false);
      fitText(x + 24 * s, cy + 28 * s, cardW, 17 * s, 12 * s, c.title, C_TEXT);
      // (a flight that starts and ends at one field - a lesson's rings, a checkpoint course - is a local flight, not "0 km")
      std::string sub = c.longHaul() ? fmt("%s  %s > %s  %.0f km", contractTypeName(c.type), g_world.airports[c.from].code, arrivalAirport(c).code, c.haulKm)
                      : c.from == c.to ? fmt("%s  %s  local flight", contractTypeName(c.type), g_world.airports[c.from].code)
                                       : fmt("%s  %s > %s  %.0f km", contractTypeName(c.type), g_world.airports[c.from].code, g_world.airports[c.to].code, g_world.distanceKm(c.from, c.to));
      float payW = g_ren.text(x + lw - 24 * s, cy + 58 * s, 16 * s, fmtMoney(c.payout), C_GOOD, 1, 2);
      g_ren.text(x + 24 * s, cy + 58 * s, 14 * s, ellipsize(sub, cardW - payW - 12 * s, 14 * s), C_DIM, 1);
    }
    cy += step;
  }
  // details
  float dx = x + lw + 16 * s, dw = w - lw - 16 * s;
  panel(dx, y, dw, h);
  Card cd = cards[selContract];
  if (cd.travel) { drawTravel(dx, y, dw, h); return; }
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
  float mapW = std::min(iw * 0.32f, h * 0.39f);
  float textW = iw - mapW - 20 * s;
  {
    auto tl = wrap(c.title, textW, 24 * s);
    if (tl.size() > 2) { tl.resize(2); tl[1] = ellipsize(tl[1] + " ...", textW, 24 * s); }
    for (auto& l : tl) { g_ren.text(px, py, 24 * s, l, cd.story ? C_WARN : C_TEXT, 1); py += 31 * s; }
    py += 8 * s;
  }
  // The briefing and its figures scroll (mouse wheel) in a region of their own above the aircraft chooser, whose rows
  // keep their place at the panel's foot: a long briefing never pushes the chooser off the panel or runs under it
  const float chooserH = 26 * s + ((kNumAircraft + 1) / 2) * (44 * s + 6 * s);
  const float chooserTop = std::min(std::max(y + h - 70 * s - chooserH, y + 70 * s + mapW + 8 * s), y + h - 70 * s - 26 * s - 2 * (44 * s + 6 * s));
  // (the route map ends above the chooser: on a short panel - a large UI scale - it is drawn smaller rather than over it)
  mapW = std::max(60 * s, std::min(mapW, chooserTop - (y + 60 * s) - 16 * s));
  const float detTop = py, detBot = chooserTop - 10 * s;
  static float detScroll = 0.f, detMax = 0.f; static int detFor = -1;
  if (detFor != hubList * 1000 + selContract) { detFor = hubList * 1000 + selContract; detScroll = 0.f; detMax = 0.f; }
  bool detWheeled = false;   // (the region with its scroll bar beside it: the bar is the region's own stop in the D-pad's walk)
  if (hovered(px, detTop, textW + 14 * s, detBot - detTop) && in.wheel != 0) { detScroll -= in.wheel * 48.f * s; in.wheel = 0; detWheeled = true; }
  detScroll = std::clamp(detScroll, 0.f, detMax);
  const size_t detFocus0 = focusList.size();
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
  const Airport& A = g_world.airports[c.from]; const Airport& B = arrivalAirport(c);
  row("Route", fmt("%s %s  >  %s %s", A.code, A.name, B.code, B.name));
  if (c.longHaul()) { const auto to = procedural::find(c.toRegion); row("Distance", fmt("%.0f km to %s", c.haulKm, to ? earthCharts::regionPhrase(*to).c_str() : "the next region")); }
  else row("Distance", c.from == c.to ? (c.wps.empty() ? std::string("Local flight") : fmt("Local flight, %d checkpoint%s", (int)c.wps.size(), c.wps.size() == 1 ? "" : "s"))
                                 : fmt("%.1f km", g_world.distanceKm(c.from, c.to)));
  row("Destination", fmt("Rwy %02d/%02d, %.0f m %s, elev %s", B.rwyNumber(false), B.rwyNumber(true), B.length, surfaceName(B.surface), fmtAlt(B.elev).c_str()));
  if (c.cargoKg || c.pax) row("Load", c.pax ? fmt("%d passenger%s, %d kg", c.pax, c.pax == 1 ? "" : "s", c.cargoKg) : fmt("%d kg cargo%s", c.cargoKg, c.fragile ? " (FRAGILE)" : ""));
  if (c.timeLimitMin > 0) row("Deadline", fmt("%.0f minutes", c.timeLimitMin), C_BAD);
  row("Weather", c.wx.describe());
  if (c.wxShift) row("Forecast", "by arrival: " + c.wxEnd.describe(), C_WARN);
  if (cd.trial >= 0) {   // the local board
    auto it = trialBest.find(trialKey(cd.trial));
    std::string b;
    if (it != trialBest.end()) for (size_t i = 0; i < it->second.size(); i++) b += (i ? "   " : "") + fmt("%d. %s", (int)i + 1, trialScore(cd.trial, it->second[i]).c_str());
    row("Best", b.empty() ? "no time set yet" : b, C_GOOD);
    if (isCareerAircraft(selAircraft) || (selAircraft >= 0 && selAircraft < kAircraftCount)) {   // the medals' marks for the aircraft picked
      float m[3]; trialMedalMarks(cd.trial, selAircraft, m);
      row("Medals", fmt("Gold %s   Silver %s   Bronze %s", trialScore(cd.trial, m[0]).c_str(), trialScore(cd.trial, m[1]).c_str(), trialScore(cd.trial, m[2]).c_str()), vec3(1.f, 0.82f, 0.35f));
    }
    row("Counts for", "nothing: no pay, no fees, no logbook entry", C_DIM);
  }
  if (c.payout) row("Payment", c.repBonusPct > 0 ? fmt("%s (incl. +%d%% reputation bonus)", fmtMoney(c.payout).c_str(), c.repBonusPct) : fmtMoney(c.payout), C_GOOD);
  if (isCareerAircraft(selAircraft)) {   // for the aircraft picked below (last frame's choice)
    auto esrc = career.canFly(c, selAircraft);
    if (esrc != Career::SRC_NONE) {
      Career::LaunchPlan e = menuLaunchPlan(c, selAircraft, esrc, cd.job, true);
      int ops = e.fees() + e.fuelCostEst;
      if (c.payout > 0 || ops > 0) {
        std::string parts;
        auto part = [&](const char* n, int v) { if (v) parts += (parts.empty() ? "" : ", ") + fmt("%s %s", n, fmtMoney(v).c_str()); };
        part("hire", e.hire); part("positioning", e.positioning); part("ferry", e.ferry); part("fuel ~", e.fuelCostEst);
        row("Operating costs", ops ? fmt("%s (%s)", fmtMoney(ops).c_str(), parts.c_str()) : std::string(e.fuel == Career::LaunchPlan::FUEL_INCLUDED ? "none - fuel included" : "none"), C_DIM);
        row("Est. net", fmtMoney(e.net), e.net >= 0 ? C_GOOD : C_BAD);
      }
      row("Est. time", fmt("about %.0f min (+- %.0f)%s", e.minutesEst, e.minutesSigma, e.flown || c.forceAircraft >= 0 ? "" : e.flownFailed ? "  - estimated" : "  - flying it on the autopilot..."), e.mayBeLate(c.timeLimitMin) ? C_BAD : C_TEXT);
      {   // fuel and weight: the tanks at take-off (arrows: 5% of the tanks a step), the take-off weight against the
          // limit, the roll it needs here, and the uplift's price for an owned aircraft
        const AircraftSpec& sp = career.specFor(selAircraft, esrc);   // (your own as fitted: its tanks, its floats)
        if (!sp.special) {
          float fuel = chosenFuel(c, selAircraft, esrc, e);
          float payload = (float)c.cargoKg + c.pax * 85.f + 85.f, mass = sp.emptyMass + fuel + payload;
          bool heavy = mass > sp.maxMass() + 0.5f;
          const PerfModel& P = Plane::perf(&sp);
          float sigma = isaDensity(g_world.airports[c.from].elev) / 1.225f;
          float roll = P.toRoll > 0 ? P.toRoll * (mass / sp.maxMass()) * (mass / sp.maxMass()) / sigma : 0.f;
          Career::LaunchPlan ef = e; career.planFuel(ef, c, fuel);
          // (with the hours and minutes it lasts at cruise: the tanks hold real fuel, and a short hop leaves with a part load)
          const int endur = (int)cruiseMinutes(sp, fuel);
          std::string v = fmt("%.0f kg (%.0f%%, %d h %02d at cruise)  -  take-off %.0f of %.0f kg%s", fuel, 100.f * fuel / sp.maxFuel, endur / 60, endur % 60, mass, sp.maxMass(), heavy ? "  OVERWEIGHT" : "");
          if (c.rationKm > 0) v = fmt("Rationed: %.0f kg, for %.0f km  -  take-off %.0f of %.0f kg", fuel, c.rationKm, mass, sp.maxMass());
          if (roll > 0) v += fmt(", roll ~%.0f m", roll);
          if (ef.fuel == Career::LaunchPlan::FUEL_PURCHASED) v += ef.fuelUpliftKg > 0.5f ? fmt(", uplift %.0f kg for %s", ef.fuelUpliftKg, fmtMoney(ef.fuelCostEst).c_str()) : " (tanks hold it)";
          if (c.rationKm <= 0) {   // the fuel row: with the D-pad's focus on it, left / right take 5% off or put it on (the arrows are the mouse's)
            int step = 0; focusAdjust(uid(0, (float)selAircraft, "job-fuel"), px, py - 4 * s, textW, 24 * s, &step);
            if (step) launchFuelKg = std::clamp(fuel + step * sp.maxFuel * 0.05f, sp.maxFuel * 0.1f, sp.maxFuel);
            const size_t arrows = focusList.size();
            if (inView(py - 4 * s, 24 * s) && button(px + 130 * s - 36 * s, py - 4 * s, 28 * s, 24 * s, "<")) launchFuelKg = std::max(sp.maxFuel * 0.1f, fuel - sp.maxFuel * 0.05f);
            if (inView(py - 4 * s, 24 * s) && button(px + textW - 30 * s, py - 4 * s, 28 * s, 24 * s, ">")) launchFuelKg = std::min(sp.maxFuel, fuel + sp.maxFuel * 0.05f);
            focusList.resize(arrows);
          }
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
  if (c.ownedOnly || c.requireSpec >= 0) row("Requirement", c.requireSpec < 0 ? std::string("Your own aircraft") : fmt("%s%s", c.ownedOnly ? "Your own " : "The ", kAircraft[c.requireSpec].name), C_ACCENT);
  if (c.grantLicense > career.license) row("Reward", std::string("Earns ") + licenseName(c.grantLicense), C_ACCENT);
  if (c.from != career.location && c.type != CT_LESSON) { int pc = career.positioningCost(c); row("Positioning", pc ? fmt("Airline ticket to %s: %s", A.code, fmtMoney(pc).c_str()) : "Free courtesy ride", C_DIM); }
  g_ren.uiClipOff();
  detMax = std::max(0.f, py + detScroll - detBot);   // (how far the region scrolls: next frame's limit)
  if (detMax > 0.f) clipFade(px, detBot, textW, 18 * s, panelAt(y, h, detBot));
  focusScrollPixels(detFocus0, uid(0, 0, "job-details-scroll"), px, detTop, textW + 14 * s, detBot, detScroll, detWheeled, detMax > 0.f);
  if (detMax > 0.f) {   // a thin bar beside the text: where the view is in the briefing
    const float trackH = detBot - detTop, barH = std::max(20 * s, trackH * trackH / (trackH + detMax));
    g_ren.rect(px + textW + 8 * s, detTop, 3 * s, trackH, C_ACCENT, 0.12f);
    g_ren.rect(px + textW + 8 * s, detTop + (trackH - barH) * detScroll / detMax, 3 * s, barH, C_ACCENT, 0.7f);
  }
  {   // the route map in a hairline frame with corner ticks and a caption
    float mx = dx + dw - mapW - 22 * s, my = y + 60 * s;
    const std::vector<Waypoint> rest(c.wps.begin() + std::clamp(c.wpStart, 0, (int)c.wps.size()), c.wps.end());   // (a job's next leg: the checkpoints still to fly)
    drawMapView(mx, my, mapW, mapW, c.from, c.longHaul() ? c.from : c.to, &rest);
    if (c.longHaul()) {   // a long haul: its way off the map, toward the destination beyond it
      const auto here = procedural::find(career.region);
      const vec3 far = regionTravel::Chart(here ? here->origin : geo::solaceOrigin()).local(airportPlace(c.toRegion, arrivalAirport(c)));
      const Airport& a = g_world.airports[c.from];
      vec2 d(far.x - a.x, far.z - a.z); d = d * (1.f / std::max(length(d), 1.f));
      const vec2 p0(mx + (a.x + WORLD_HALF) / (2 * WORLD_HALF) * mapW, my + (a.z + WORLD_HALF) / (2 * WORLD_HALF) * mapW);
      float t = 1e9f;   // (to the square's edge)
      if (d.x > 1e-4f) t = std::min(t, (mx + mapW - p0.x) / d.x); if (d.x < -1e-4f) t = std::min(t, (mx - p0.x) / d.x);
      if (d.y > 1e-4f) t = std::min(t, (my + mapW - p0.y) / d.y); if (d.y < -1e-4f) t = std::min(t, (my - p0.y) / d.y);
      const vec2 p1 = p0 + d * std::max(t - 4 * s, 0.f);
      g_ren.line(p0.x, p0.y, p1.x, p1.y, 3 * s, C_ACCENT, 0.9f);
      g_ren.rect(p1.x - 5 * s, p1.y - 5 * s, 10 * s, 10 * s, C_WARN, 1, 5 * s);
    }
    g_ren.rectOutline(mx - 4 * s, my - 4 * s, mapW + 8 * s, mapW + 8 * s, C_ACCENT, 0.2f, 2 * s, 1 * s);

    g_ren.text(mx - 4 * s, my - 22 * s, 11.5f * s, ellipsize(fmt(c.longHaul() ? "%s > %s  %.0f km" : "%s > %s", g_world.airports[c.from].code, arrivalAirport(c).code, c.haulKm), mapW, 11.5f * s), C_ACCENT, 0.9f, 0, false);
  }
  // aircraft selection
  py = chooserTop;
  header(px, py, iw, "CHOOSE AIRCRAFT"); const float chooserY = py; py += 26 * s;
  // every aircraft's eligibility first, then the flyable ones listed ahead of the rest: a panel too short for all the
  // rows drops unavailable aircraft, never one the job can be flown in (the review of v3.24.0: an owned Emissary, the
  // seventh row, went unseen and unselectable on a long briefing)
  int firstOk = -1;
  std::vector<Career::Source> srcs(kAircraftCount); std::vector<std::string> whys(kAircraftCount);
  std::vector<int> order(kNumAircraft); int nOrder = 0;
  for (int i : kCareerAircraft) {
    srcs[i] = career.canFly(c, i, &whys[i]);
    if ((c.type == CT_FERRY || c.type == CT_TRIAL) && srcs[i] == Career::SRC_NONE && career.license == LIC_STUDENT && i == 0) srcs[i] = Career::SRC_LESSON;
  }
  // the player's own aircraft first, then the other flyable ones, then the rest
  for (int pass = 0; pass < 3; pass++)
    for (int i : kCareerAircraft) {
      const bool own = srcs[i] == Career::SRC_OWNED, ok = srcs[i] != Career::SRC_NONE;
      if ((pass == 0 && own) || (pass == 1 && ok && !own) || (pass == 2 && !ok)) { order[nOrder++] = i; if (ok && firstOk < 0) firstOk = i; }
    }
  float rowH = 44 * s, rowStep = rowH + 6 * s;
  float colW = (iw - 10 * s) * 0.5f;
  // the rows scroll (mouse wheel, or the right stick) when the panel can't show them all: every aircraft stays
  // reachable (the review of v3.31.0, U1: an owned Wavecrest sat below six rentals, out of reach)
  const float chooserBot = y + h - 70 * s;
  const int totalRows = (kNumAircraft + 1) / 2, visRows = std::max(1, (int)floorf((chooserBot - py + 6 * s) / rowStep));
  static int chooserScroll = 0; static int chooserFor = -1;
  if (chooserFor != hubList * 1000 + selContract) { chooserFor = hubList * 1000 + selContract; chooserScroll = 0; }
  bool chooserWheeled = false;
  if (hovered(px, py, iw, visRows * rowStep) && in.wheel != 0) { chooserScroll -= (int)in.wheel; in.wheel = 0; chooserWheeled = true; }
  // the D-pad walks the aircraft (A takes one; one it can't fly shows why): stepping off the window's edge scrolls the
  // rows (the row just past each edge is in the walk), the right stick scrolling them takes the focus along
  auto craftId = [&](int j) { return uid(0, (float)order[j], "job-aircraft"); };
  focusScrollRows(craftId, kNumAircraft, chooserScroll, visRows, 2, chooserWheeled);
  chooserScroll = std::clamp(chooserScroll, 0, std::max(0, totalRows - visRows));
  for (int r : {chooserScroll - 1, chooserScroll + visRows}) for (int c2 = 0; c2 < 2; c2++) {
    const int j = r * 2 + c2;
    if (r >= 0 && j < kNumAircraft) focusList.push_back({craftId(j), px + c2 * (colW + 10 * s), py + (r - chooserScroll) * rowStep, colW, rowH});
  }
  int hidden = 0, below = 0, above = chooserScroll * 2;
  // a row whose name or status had to be shortened shows them in full under it while it's pointed at, or while it's
  // the chosen one and nothing else is (the review of v3.33.0, U2: at 140% the names and the reasons an aircraft
  // can't take a job were cut to a few letters)
  int tipJ = -1; bool tipHov = false; float tipX = 0, tipY = 0; std::string tipName, tipSt;
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
    bool take = false; focusHere(craftId(j), rx, ry, colW, rowH, &take);
    if (((hov && !focusNav && in.mPressed[0]) || take) && src != Career::SRC_NONE) { selAircraft = i; launchFuelKg = -1; if (!take) g_audio.trigger(SFX_CLICK); }
    // (the name and the status share the row: each kept to its side, shortened when the row is narrow)
    const float nameW = g_ren.textWidth(kAircraft[i].name, 15 * s), nameMax = colW - 24 * s;
    g_ren.text(rx + 10 * s, ry + 7 * s, 15 * s, nameW > nameMax ? ellipsize(kAircraft[i].name, nameMax, 15 * s) : std::string(kAircraft[i].name), src == Career::SRC_NONE ? C_DIM * 0.6f : C_TEXT, 1);
    std::string st;
    if (src == Career::SRC_LESSON) st = "School aircraft";
    else if (src == Career::SRC_OWNED) { int fc = career.ferryCost(c, i); st = fc ? fmt("Owned (ferry %s)", fmtMoney(fc).c_str()) : "Owned"; }
    else if (src == Career::SRC_RENT) st = fmt("Rent %s", fmtMoney(kAircraft[i].rentFee).c_str());
    else st = why;
    float sts = 12.5f * s;
    const float stMax = colW - 24 * s;
    const std::string stFull = st;
    while (g_ren.textWidth(st, sts) > stMax && st.size() > 4) st = st.substr(0, st.size() - 4) + "...";
    g_ren.text(rx + 10 * s, ry + 27 * s, 11.5f * s, st, src == Career::SRC_NONE ? C_DIM : src == Career::SRC_OWNED ? C_GOOD : C_WARN, 1);
    const bool pointed = hovered(rx, ry, colW, rowH);
    if ((nameW > nameMax || st != stFull) && (pointed || (sel && !tipHov))) { tipJ = j; tipHov = pointed; tipX = rx; tipY = ry + rowH; tipName = kAircraft[i].name; tipSt = stFull; }
  }
  if (tipJ >= 0) {
    const float tw = std::min(iw, std::max(colW, g_ren.textWidth(tipName, 14 * s) + 24 * s)), tx = std::min(tipX, px + iw - tw);
    auto lines = wrap(tipSt, tw - 20 * s, 12.5f * s);
    const float th = 28 * s + 17 * s * lines.size(), ty = tipY + th + 4 * s > y + h ? tipY - rowH - th - 2 * s : tipY + 2 * s;
    g_ren.rect(tx, ty, tw, th, vec3(0.02f, 0.03f, 0.05f), 0.96f, 6 * s);
    g_ren.rectOutline(tx, ty, tw, th, C_ACCENT, 0.6f, 6 * s, 1 * s);
    g_ren.text(tx + 10 * s, ty + 6 * s, 14 * s, tipName, C_TEXT, 1);
    for (size_t k = 0; k < lines.size(); k++) g_ren.text(tx + 10 * s, ty + 26 * s + 17 * s * k, 12.5f * s, lines[k], srcs[order[tipJ]] == Career::SRC_NONE ? C_BAD : C_DIM, 1);
  }
  if (selAircraft >= 0 && (!isCareerAircraft(selAircraft) || srcs[selAircraft] == Career::SRC_NONE)) selAircraft = -1;
  if (selAircraft < 0) selAircraft = firstOk;
  {   // what is scrolled out of view, on the header's line; the chosen aircraft named there whenever its row is out of view
    bool selShown = false;
    for (int j = above; j < kNumAircraft - below; j++) selShown |= order[j] == selAircraft;
    std::string note;
    if (above + below > 0) note = fmt("%s%d more%s - scroll", above > 0 ? "^ " : "", above + below, below > 0 ? " v" : "") + (hidden > 0 ? fmt(" (%d not available here)", hidden) : std::string());
    if (selAircraft >= 0 && !selShown) note = std::string("flying the ") + kAircraft[selAircraft].name + (note.empty() ? "" : "   " + note);
    if (!note.empty()) g_ren.text(px + iw, chooserY + 2 * s, 12 * s, ellipsize(note, std::max(60 * s, iw - g_ren.textWidth("CHOOSE AIRCRAFT", 12 * s) - 24 * s), 12 * s), selAircraft >= 0 && !selShown ? C_GOOD : C_DIM, 1, 2);
  }
  bool otherWhileJob = openJob && !cd.job && !cd.free;   // (another job waits until this one is delivered or released)
  bool overweight = false;
  if (selAircraft >= 0) {
    auto esrc = career.canFly(c, selAircraft); const AircraftSpec& sp = career.specFor(selAircraft, esrc);
    if (esrc != Career::SRC_NONE) { Career::LaunchPlan e0 = menuLaunchPlan(c, selAircraft, esrc, cd.job, false); float fuel = e0.fuelLoadKg; overweight = sp.emptyMass + fuel + c.cargoKg + c.pax * 85.f + 85.f > sp.maxMass() + 0.5f; }
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
    if (cd.trial >= 0) startTrial(cd.trial, selAircraft, (int)src);   // (a trial: off the books, its ghost loaded)
    else if (cd.job) continueJob(selAircraft, src); else beginCareerFlight(go, selAircraft, src);
  }
  // (the line beside the buttons, in the width left of them: on two lines when it needs them, never under them)
  auto status = [&](const std::string& t, vec3 col) {
    const float sw = std::max(120 * s, dx + dw - 262 * s - (cd.job ? 300 * s : 0.f) - 12 * s - px);
    auto lines = wrap(t, sw, 14 * s);
    if (lines.size() > 2) { lines.resize(2); lines[1] = ellipsize(lines[1] + " ...", sw, 14 * s); }
    float ly = y + h - (lines.size() > 1 ? 60 : 50) * s;
    for (auto& l : lines) { g_ren.text(px, ly, 14 * s, l, col, 1); ly += 18 * s; }
  };
  if (commitBlocked()) status("Your last result isn't saved yet (" + saveWhy + "). Retrying...", C_BAD);
  else if (otherWhileJob) status("Deliver or release the job in progress first (its card is at the top).", C_WARN);
  else if (overweight) status("Over the take-off weight limit: take less fuel (the arrows by the fuel row), or a bigger aircraft.", C_BAD);
  else if (!can) status("Tip: check the Hangar to buy an aircraft, or earn your next licence through the story.", C_DIM);
}

bool Game::menuPreviewTarget() const {
  uint32_t id = 0; float x=0,y=0,w=0,h=0;
  if (screen == SCR_RESEARCH && resAuthed && !resWarm) {
    const auto L=researchLayout(); id=0x7E5F0030u;
    x=L.px0;y=L.top;w=L.px1-L.px0;h=L.bot-56*S()-y;
  } else if (screen == SCR_HUB && hubTab == TAB_CONTRACTS && hubList == 2) {
    const auto L=militaryLayout(); const float s=std::min(S(),std::min(g_ren.H/720.f,g_ren.W/1120.f));
    id=0x6D0020u;x=L.px0;y=L.top+79*s;w=L.px1-L.px0;h=L.bot-56*s-y;
  } else if (screen == SCR_FREE_FLIGHT || (screen == SCR_HUB && hubTab == TAB_HANGAR)) {
    const float s=S(); const auto L=hangarLayout(24*s,126*s,g_ren.W-48*s,g_ren.H-146*s,s);
    id=0x7E6F0030u;x=L.previewX;y=L.previewY;w=L.previewWidth;h=L.previewHeight;
  }
  if (!id) return false;
  return focusNav ? focusId>=id && focusId<id+5 : hovered(x,y,w,h);
}

void Game::updateMenuPreview(MenuPreviewView& view,float x,float y,float w,float h,uint32_t firstId) {
  if (!windowFocused) { view.release(); return; }
  const bool room = &view == &hangarView;
  const float minPitch = room || (&view == &researchView && !resAirborne) ? .04f : -.5f;
  const float maxPitch = room ? .35f : 1.f;
  // Store the angle the camera can actually display; reversing at a floor/roof limit has no hidden dead travel.
  view.pitch = clampf(view.pitch, minPitch, maxPitch);
  const bool over=hovered(x,y,w,h), focused=focusNav && focusId>=firstId && focusId<firstId+5;
  if (over && !focusNav && in.mPressed[0]) { view.drag=true; view.held=-1; in.mPressed[0]=false; }
  if (!in.mDown[0] || focusNav) view.drag=false;
  if (view.drag) { view.rotate(in.mdx*.008f); view.elevate(in.mdy*.006f); }
  // Wheel/analogue input is local to the stage or its focused controls. Lists and
  // equipment rails retain the right stick's ordinary scrolling elsewhere.
  if (over && in.wheel!=0) { view.magnify(in.wheel*.1f); in.wheel=0; }
  if (in.pad && (focused || (!focusNav && over))) {
    auto axis=[](float v) { return std::fabs(v)<.15f?0.f:(v-std::copysign(.15f,v))/.85f; };
    const float dt=clampf(uiDt,0.f,.1f);
    const float rx=axis(in.rx),ry=axis(in.ry),zoom=std::max(0.f,in.rt-.15f)-std::max(0.f,in.lt-.15f);
    if (rx) view.rotate(rx*dt*2.f);
    if (ry) view.elevate(-ry*dt*1.2f);
    if (zoom) view.magnify(zoom*dt*.8f);
  }
  view.pitch = clampf(view.pitch, minPitch, maxPitch);
}

void Game::drawMenuPreviewControls(MenuPreviewView& view,uint32_t firstId,float x,float y,float w,
    float s,vec3 panelColour,vec3 accent,vec3 textColour,vec3 dim,float radius,float alpha) {
  const auto L=menuPreviewControls(x,y,w,s);
  g_ren.rect(x,y,w,56*s,panelColour,.94f*alpha,radius);
  const float labelSize=std::max(8.f,9*s);
  g_ren.text(L.x+L.buttonWidth+3*s,y+8*s,labelSize,"ROTATE",dim,alpha,1,false);
  g_ren.text(L.x+3*L.buttonWidth+15*s,y+8*s,labelSize,fmt("ZOOM %.2fx",view.zoom),dim,alpha,1,false);
  const char* labels[]={"<",">","-","+","RESET"};
  bool holding=false;
  for (int i=0;i<5;++i) {
    const uint32_t id=firstId+uint32_t(i); const float bx=L.x+i*L.step;
    focusList.push_back({id,bx,L.buttonY,L.buttonWidth,L.buttonHeight});
    const bool focus=focusNav && focusId==id, over=hovered(bx,L.buttonY,L.buttonWidth,L.buttonHeight);
    const bool hot=focus || (!focusNav && over);
    const float glow=anim(id,hot?1.f:0.f,14);
    g_ren.rectGrad(bx,L.buttonY,L.buttonWidth,L.buttonHeight,panelColour+accent*(.07f+.13f*glow),panelColour,.98f*alpha,radius);
    g_ren.rectOutline(bx,L.buttonY,L.buttonWidth,L.buttonHeight,accent,(.30f+.5f*glow)*alpha,radius,s);
    if(focus)g_ren.rectOutline(bx-2*s,L.buttonY-2*s,L.buttonWidth+4*s,L.buttonHeight+4*s,accent,.95f*alpha,radius,1.5f*s);
    const float fs=std::min({std::max(10.f,12.f*s),L.buttonHeight*.65f,(L.buttonWidth-6*s)/(i==4?3.2f:1.f)});
    g_ren.text(bx+L.buttonWidth*.5f,L.buttonY+(L.buttonHeight-fs)*.5f,fs,labels[i],hot?textColour:accent,alpha,1,false);
    const bool key=windowFocused && focus && (in.pressed[K_ENTER] || in.pressed[' ']);
    const bool mouse=windowFocused && !focusNav && over && in.mPressed[0];
    if (key || mouse) {
      const int source=mouse?1:(in.pad && (in.buttons&PAD_A))?4:in.pressed[' ']?3:2;
      view.action(i); g_audio.trigger(SFX_CLICK);
      if(i<4) { view.held=i;view.heldSource=source;view.holdTime=-.35f; }
      in.mPressed[0]=false;
      if(key) in.pressed[K_ENTER]=in.pressed[' ']=false;
    }
    if (i==view.held) {
      const bool down=windowFocused && (view.heldSource==1 ? !focusNav && over && in.mDown[0] : focus &&
        (view.heldSource==4 ? bool(in.buttons&PAD_A) : view.heldSource==3 ? in.down[' '] : in.down[K_ENTER]));
      if(down) {
        holding=true;view.holdTime+=clampf(uiDt,0.f,.1f);
        while(view.holdTime>=0.f) { view.action(i);view.holdTime-=.08f; }
      }
    }
  }
  if(!holding) { view.held=-1;view.heldSource=0;view.holdTime=0; }
}

void Game::drawHangarPreviewControls(float x,float y,float w) {
  const float s=S();const auto L=hangarLayout(24*s,126*s,g_ren.W-48*s,g_ren.H-146*s,s);
  updateMenuPreview(hangarView,L.previewX,L.previewY,L.previewWidth,L.previewHeight,0x7E6F0030u);
  drawMenuPreviewControls(hangarView,0x7E6F0030u,x,y,w,s,C_PANEL,C_ACCENT,C_TEXT,C_DIM,4*s);
}

// The travel card (Career::destinations): the regions the airline flies to from where the career is - the islands
// first when away, then the nearest - each with its fare and the licence it takes. A click buys the ticket
// (Career::travel); the hub then waits behind the loading card while the region is prepared (Game::screenWorld)
void Game::drawTravel(float x, float y, float w, float h) {
  float s = S();
  const float px = x + 22 * s, iw = w - 44 * s;
  float py = y + 18 * s;
  g_ren.text(px, py, 24 * s, career.atHome() ? "Beyond the islands" : "Travel on, or home", C_TEXT, 1);
  // the world beyond the islands, when the Earth pack is there: chosen at home (Career::chooseWorld)
  if (earthCharts::pack() && career.atHome() &&
      button(px + iw - 230 * s, py - 2 * s, 230 * s, 32 * s, career.realEarth ? "Beyond: the real Earth" : "Beyond: generated regions", !commitBlocked(), career.realEarth)) {
    std::string m; bool ok = false;
    commit([&](Career& k) { ok = k.chooseWorld(!k.realEarth, &m); });
    hubMsg = m; hubMsgTime = 6;
    if (ok) g_audio.trigger(SFX_CLICK);
  }
  py += 40 * s;
  const std::string brief = career.realEarth ? (career.atHome()
    ? "Fly with the airline to a real airport and work there: its own board, the fields round it, and pay 15% above the islands'. Bermuda is 640 km west; the American coast lies beyond. Your own aircraft stay parked where they are; rentals wait at every field. The campaign, the trials, Defense Command and your airline stay in the Solace Islands."
    : "Fly on to another real airport, or home to the Solace Islands for the campaign, the trials, Defense Command and your airline. Your own aircraft stay parked where they are.") : career.atHome()
    ? "Fly with the airline to another region and work there: its own airports, its own board, and pay 15% above the islands'. Your own aircraft stay parked where they are; rentals wait at every field. The campaign, the trials, Defense Command and your airline stay in the Solace Islands."
    : "Fly on to another region, or home to the Solace Islands for the campaign, the trials, Defense Command and your airline. Your own aircraft stay parked where they are.";
  for (auto& l : wrap(brief, iw, 15 * s)) { g_ren.text(px, py, 15 * s, l, C_TEXT, 0.92f); py += 21 * s; }
  py += 8 * s;
  const bool busy = career.job.has_value() || career.attemptOpen;
  if (busy) { g_ren.text(px, py, 14 * s, "Finish or release the open job before you travel.", C_WARN, 1); py += 26 * s; }
  static std::string listFor; static int listLic = -1, listWorld = -1; static std::vector<Career::Destination> list;
  if (listFor != career.region || listLic != career.license || listWorld != (int)career.realEarth) {
    list = career.destinations(); listFor = career.region; listLic = career.license; listWorld = (int)career.realEarth;
  }
  header(px, py, iw, "DESTINATIONS"); py += 30 * s;
  const float top = py, bottom = y + h - 16 * s, rowH = 56 * s;
  static float scroll = 0.f;
  const float limit = std::max(0.f, list.size() * rowH - (bottom - top));
  bool wheeled = false;
  if (hovered(px, top, iw, bottom - top) && in.wheel != 0) { scroll -= in.wheel * 48 * s; in.wheel = 0; wheeled = true; }
  scroll = std::clamp(scroll, 0.f, limit);
  const size_t focus0 = focusList.size();
  g_ren.uiClip(px - 4 * s, top, px + iw + 4 * s, bottom);
  float ry = top - scroll;
  for (const Career::Destination& d : list) {
    const auto region = procedural::find(d.id);
    const int needs = d.home ? LIC_STUDENT : std::max(d.minLicense, (int)LIC_PPL);
    const bool licensed = career.license >= needs, credit = d.home && career.money < d.fare;
    const bool can = !busy && licensed && (credit || career.money >= d.fare) && !commitBlocked();
    if (ry + rowH > top && ry < bottom) {
      g_ren.rect(px, ry, iw, rowH - 6 * s, d.home ? C_ACCENT * 0.12f : C_PANEL2, d.home ? 0.8f : 0.45f, 4 * s);
      g_ren.text(px + 12 * s, ry + 7 * s, 15 * s, ellipsize(d.name, iw - 210 * s, 15 * s), licensed ? C_TEXT : C_DIM, 1);
      std::string sub = d.home ? std::string("Home: the campaign, the trials, Defense Command, your airline")
                               : earthCharts::isEarthId(d.id) ? std::string("The real Earth")
                               : region ? fmt("%s %s", procedural::scaleName(region->scale), lowerS(procedural::archetypeName(region->archetype)).c_str()) : std::string();
      sub += fmt("  /  %.0f km", d.km);
      if (!licensed) sub += std::string("  /  needs a ") + licenseName(needs);
      g_ren.text(px + 12 * s, ry + 29 * s, 12 * s, ellipsize(sub, iw - 210 * s, 12 * s), licensed ? C_DIM : C_WARN, 0.9f);
      if (ry >= top && ry + 44 * s <= bottom &&
          button(px + iw - 180 * s, ry + 8 * s, 170 * s, 34 * s, credit ? std::string("Fly home on account") : "Fly / " + fmtMoney(d.fare), can, d.home)) {
        std::string m; bool ok = false;
        commit([&](Career& k) { ok = k.travel(d, &m); });
        hubMsg = m; hubMsgTime = 6;
        if (ok) g_audio.trigger(SFX_CASH);
      }
    }
    ry += rowH;
  }
  g_ren.uiClipOff();
  focusScrollPixels(focus0, uid(0, 0, "travel-scroll"), px, top, iw, bottom, scroll, wheeled, limit > 0);
}

void Game::drawHubHangar(float x, float y, float w, float h) {
  const float s = S();
  // where an owned aircraft is parked: its field here (its name, or its code), or the region it is in
  auto parkedAt = [&](const OwnedPlane& p, bool name) -> std::string {
    if (career.here(p) && g_world.regionId == career.region && p.location < (int)g_world.airports.size())
      return name ? g_world.airports[p.location].name : g_world.airports[p.location].code;
    if (p.region == "solace") return "the Solace Islands";
    if (earthCharts::isEarthId(p.region)) if (const Airport* a = procedural::airportOf(p.region, p.location)) return a->name;   // (the real Earth: its field)
    if (auto d = procedural::find(p.region)) return "the " + d->name;
    return p.region;
  };
  const HangarLayout layout = hangarLayout(x, y, w, h, s);
  const float lw = layout.leftWidth, rw = layout.rightWidth;
  const float rx = layout.rightX, vx = layout.previewX, vw = layout.previewWidth;
  const int catalogCount = hangarCatalogCount();
  selHangar = hangarSpecAt(hangarRowFor(selHangar));
  const int selectedRow = hangarRowFor(selHangar);
  panel(x, y, lw, h); panel(rx, y, rw, h);
  g_ren.text(x + 18 * s, y + 18 * s, 22 * s, "Hangar", C_TEXT, 1);
  g_ren.text(x + 18 * s, y + 48 * s, 11 * s, fmt("%02d AIRFRAMES / %02d CLASSIFIED", catalogCount, 4), C_DIM, 1, 0, false);
  const float top = y + 78 * s, bottom = y + h - 30 * s, rowH = 68 * s;
  const int visible = std::max(1, (int)((bottom - top) / rowH));
  static int first = 0, lastSel = -1;
  bool catalogWheeled = false;
  if (hovered(x, top, lw, bottom - top) && in.wheel) { first -= (int)in.wheel; in.wheel = 0; catalogWheeled = true; }
  if (lastSel != selHangar) { if (selectedRow < first) first = selectedRow; if (selectedRow >= first + visible) first = selectedRow - visible + 1; lastSel = selHangar; }
  auto catalogId = [&](int spec) { return uid(0, (float)spec, "hangar-catalog-airframe"); };
  // Register the entire catalog, including offscreen rows, for arrow / D-pad navigation.
  // Identity is per airframe and never changes with scrolling or a redacted display name.
  // (the focus moving brings its row into view; the list scrolled by the right stick takes the focus along)
  focusScrollRows([&](int row) { return catalogId(hangarSpecAt(row)); }, catalogCount, first, visible, 1, catalogWheeled);
  first = std::clamp(first, 0, std::max(0, catalogCount - visible));
  for (int row = 0; row < catalogCount; ++row)
    focusList.push_back({catalogId(hangarSpecAt(row)), x + 10 * s, top + (row - first) * rowH, lw - 20 * s, rowH - 8 * s});
  for (int row = first; row < std::min(catalogCount, first + visible); ++row) {
    const int i = hangarSpecAt(row);
    const bool classified = hangarResearchLocked(i);
    const float cy = top + (row - first) * rowH;
    bool sel = selHangar == i, hov = hovered(x + 10 * s, cy, lw - 20 * s, rowH - 8 * s);
    const bool focused = focusNav && focusId == catalogId(i);
    card(x + 10 * s, cy, lw - 20 * s, rowH - 8 * s, sel, hov || focused, classified ? C_WARN : C_ACCENT);
    if (focused) g_ren.rectOutline(x + 8 * s, cy - 2 * s, lw - 16 * s, rowH - 4 * s, classified ? C_WARN : C_ACCENT, 0.85f, 5 * s, 1.5f * s);
    const bool activate = focused && (in.pressed[K_ENTER] || in.pressed[' ']);
    if ((hov && in.mPressed[0]) || activate) {
      selHangar = i; g_audio.trigger(SFX_CLICK);
      if (activate) in.pressed[K_ENTER] = in.pressed[' '] = false;
    }
    g_ren.text(x + 23 * s, cy + 10 * s, 16 * s, ellipsize(classified ? std::string(hangarResearchDesignation(i)) + " / REDACTED" : kAircraft[i].name, lw - 46 * s, 16 * s), C_TEXT, 1);
    const bool owned = !classified && career.ownedIndexFor(i) >= 0;
    g_ren.text(x + 23 * s, cy + 35 * s, 12 * s, classified ? "CLASSIFIED / LOCKED" : owned ? "IN YOUR FLEET" : fmtMoney(kAircraft[i].price), classified ? C_WARN : owned ? C_GOOD : C_DIM, 1, 0, false);
  }
  g_ren.text(x + 18 * s, y + h - 22 * s, 11 * s, ellipsize(fmt("%02d - %02d / %02d   SCROLL", first + 1, std::min(catalogCount, first + visible), catalogCount), lw - 36 * s, 11 * s), C_DIM, 0.85f, 0, false);
  const AircraftSpec& a = kAircraft[selHangar];
  const bool classified = hangarResearchLocked(selHangar);
  hangarView.select(selHangar, false);
  // The real selected aircraft is framed here by the hangar scene camera. No opaque panel covers it.
  g_ren.rectGrad(vx, y, vw, 82 * s, vec3(0.01f, 0.023f, 0.038f), vec3(0.02f, 0.04f, 0.06f), 0.84f, 4 * s);
  g_ren.text(vx + 18 * s, y + 13 * s, 11 * s, classified ? "CLASSIFIED AIRFRAME" : "AIRCRAFT INSPECTION", classified ? C_WARN : C_ACCENT, 1, 0, false);
  fitText(vx + 18 * s, y + 33 * s, vw - 36 * s, 28 * s, 16 * s, classified ? std::string(hangarResearchDesignation(selHangar)) + " / REDACTED" : a.name, C_TEXT);
  g_ren.text(vx + 18 * s, y + 66 * s, 10 * s, ellipsize(classified ? "CLASSIFIED / RESEARCH ACCESS ONLY" : a.role, vw - 36 * s, 10 * s), C_DIM, 1, 0, false);
  drawHangarPreviewControls(vx, y + h - 56 * s, vw);
  // Inspector is independently scrollable. Every economic action survives at large UI scales.
  const float px = rx + 20 * s, iw = rw - 40 * s, regTop = y + 54 * s, regBot = y + h - 24 * s;
  if (classified) {
    // This is a career-hangar teaser, not an acquisition or dispatch path. Do not evaluate
    // prices, ownership services, financing, or real specifications for these airframes.
    header(px, y + 20 * s, iw, "CLASSIFIED DOSSIER");
    g_ren.uiClip(rx + 12 * s, regTop, rx + rw - 12 * s, regBot);
    float py = regTop;
    g_ren.rect(px, py, iw, 32 * s, vec3(0.14f, 0.095f, 0.04f), 0.85f, 4 * s);
    g_ren.text(px + 12 * s, py + 10 * s, 11 * s, ellipsize("ACCESS LOCKED", iw - 24 * s, 11 * s), C_WARN, 1, 0, false);
    py += 50 * s;
    for (auto& line : wrap("Research authorization required.", iw, 17 * s)) { g_ren.text(px, py, 17 * s, line, C_TEXT, 1); py += 24 * s; }
    py += 8 * s;
    for (auto& line : wrap("Future special missions will grant access. These missions are not available yet.", iw, 13 * s)) { g_ren.text(px, py, 13 * s, line, C_DIM, 1); py += 19 * s; }
    py += 20 * s;
    const char* fields[] = {"IDENTITY", "PERFORMANCE", "SYSTEMS", "ACQUISITION"};
    for (int i = 0; i < 4; ++i) {
      if (py + 40 * s > regBot) break;
      g_ren.text(px, py, 10 * s, fields[i], C_DIM, 0.9f, 0, false);
      g_ren.rect(px, py + 18 * s, iw * (i % 2 ? 0.68f : 0.88f), 11 * s, C_DIM, 0.2f, s);
      py += 48 * s;
    }
    g_ren.uiClipOff();
    g_ren.text(px, y + h - 17 * s, 10 * s, ellipsize("ALL AIRFRAME DATA REDACTED", iw, 10 * s), C_WARN, 0.8f, 0, false);
    return;
  }
  header(px, y + 20 * s, iw, "AIRFRAME / OWNERSHIP");
  static float scroll = 0, contentH = 0; static int inspectFor = -1;
  if (inspectFor != selHangar) { inspectFor = selHangar; scroll = 0; contentH = 0; }
  bool inspectWheeled = false;
  if (hovered(rx, regTop, rw, regBot - regTop) && in.wheel) { scroll -= in.wheel * 48 * s; in.wheel = 0; inspectWheeled = true; }
  const float maxScroll = std::max(0.f, contentH - (regBot - regTop));
  scroll = std::clamp(scroll, 0.f, maxScroll);
  const size_t focus0 = focusList.size();
  hitClipOn = true; hitClip[0] = rx; hitClip[1] = regTop; hitClip[2] = rx + rw; hitClip[3] = regBot;
  g_ren.uiClip(rx + 8 * s, regTop, rx + rw - 8 * s, regBot);
  buttonContentOffsetY = scroll;
  float py = regTop - scroll;
  auto note = [&](const std::string& text, vec3 color = C_DIM) { for (auto& line : wrap(text, iw, 12 * s)) { g_ren.text(px, py, 12 * s, line, color, 1); py += 17 * s; } py += 7 * s; };
  auto row = [&](const std::string& k, const std::string& v) {
    g_ren.text(px, py, 11 * s, k, C_DIM, 1, 0, false); py += 19 * s;
    for (auto& line : wrap(v, iw, 14 * s)) { g_ren.text(px + iw, py, 14 * s, line, C_TEXT, 1, 2); py += 19 * s; }
    g_ren.rect(px, py + 5 * s, iw, s, C_DIM, 0.12f); py += 17 * s;
  };
  // Compact paired headline metrics, followed by the complete technical specification.
  const float half = (iw - 14 * s) * 0.5f;
  auto metric = [&](float mx, const char* label, const std::string& value) {
    g_ren.rect(mx, py, half, 58 * s, C_PANEL2, 0.75f, 4 * s);
    g_ren.text(mx + 10 * s, py + 9 * s, 10 * s, label, C_DIM, 1, 0, false);
    g_ren.text(mx + 10 * s, py + 29 * s, 17 * s, ellipsize(value, half - 20 * s, 17 * s), C_TEXT, 1);
  };
  // (your own as you've fitted it at the mechanic's: its floats, its kits)
  int oi = career.ownedIndexFor(selHangar);
  const AircraftSpec& af = oi >= 0 ? career.airframe(oi) : a;
  metric(px, "CRUISE", fmtSpeed(af.cruise)); metric(px + half + 14 * s, "RANGE", fmt("%.0f km", af.rangeKm)); py += 72 * s;
  header(px, py, iw, "ACQUISITION"); py += 28 * s;
  auto result = [&](const std::string& m, bool cash) { if (cash) g_audio.trigger(SFX_CASH); hubMsg = m; hubMsgTime = 5; };
  if (oi < 0) {
    const bool lic = career.license >= a.license && !commitBlocked();
    const bool can = lic && career.money >= a.price;
    if (button(px, py, iw, 38 * s, "Buy new / " + fmtMoney(a.price), can, can)) { std::string m; bool ok = false; commit([&](Career& k) { ok = k.buy(selHangar, &m); }); result(m, ok); }
    py += 48 * s;
    if (!can) note(commitBlocked() ? "Last result not saved yet. Retrying." : career.license < a.license ? std::string("Requires ") + licenseName(a.license) : "Insufficient funds for a new aircraft.", C_WARN);
    bool canFin = lic && !career.loan.open() && career.money >= career.downPayment(selHangar);
    if (button(px, py, iw, 36 * s, "Finance / " + fmtMoney(career.downPayment(selHangar)) + " down", canFin)) { std::string m; bool ok = false; commit([&](Career& k) { ok = k.finance(selHangar, &m); }); result(m, ok); }
    py += 46 * s;
    note(career.loan.open() ? "One loan at a time." : fmt("%s per flight for %d flights, %.0f%% interest.", fmtMoney(career.loanPayment(selHangar)).c_str(), Career::kLoanTerm, career.loanRate() * 100));
    if (button(px, py, iw, 36 * s, "Buy used / " + fmtMoney(career.usedPrice(selHangar)), lic && career.money >= career.usedPrice(selHangar))) { std::string m; bool ok = false; commit([&](Career& k) { ok = k.buyUsed(selHangar, &m); }); result(m, ok); }
    py += 46 * s; note("Two thirds condition, half tanks. Lower purchase price, more upkeep.");
  } else {
    note(std::string("Parked at ") + parkedAt(career.fleet[oi], true), career.here(career.fleet[oi]) ? C_GOOD : C_WARN);
    const int saleVal = career.saleValue(oi);
    if (career.loan.open() && career.loan.spec == career.fleet[oi].spec) note(fmt("Loan settlement %s. Net proceeds %s.", fmtMoney(career.loan.balance).c_str(), fmtMoney(saleVal - career.loan.balance).c_str()), C_WARN);
    if (button(px, py, iw, 38 * s, "Sell / " + fmtMoney(saleVal), !commitBlocked())) { std::string m; bool ok = false; commit([&](Career& k) { ok = k.sell(oi, &m); }); result(m, ok); }
    py += 52 * s;
  }
  // the mechanic (docs/HANGAR.md): floats where the type takes them, and the kits - each bought once, worked where
  // the aircraft stands (here, off any route, out of any job)
  if (oi >= 0) {
    const OwnedPlane f = career.fleet[oi];
    header(px, py, iw, "MECHANIC"); py += 28 * s;
    std::string why; const bool ready = !commitBlocked() && career.workshopReady(oi, &why);
    std::string kits;
    for (int k = 0; k < KIT_COUNT; k++) if (f.fit.has(k)) kits += (kits.empty() ? "" : ", ") + std::string(kitName(k, a));
    note(std::string(f.fit.floats ? "On amphibious floats" : "On its wheels") + (kits.empty() ? ", as it left the factory." : ". Fitted: " + kits + "."), C_GOOD);
    if (!ready) note(commitBlocked() ? "Last result not saved yet. Retrying." : why, C_WARN);
    if (floatKitFor(f.spec)) {
      if (!f.floatKit) {
        const int cost = career.floatKitPrice(f.spec);
        if (button(px, py, iw, 36 * s, "Amphibious floats / " + fmtMoney(cost), ready && career.money >= cost)) { std::string m; bool ok = false; commit([&](Career& k) { ok = k.buyFloats(oi, &m); }); result(m, ok); }
        py += 46 * s;
        note("Floats with wheels that fold up into them: seaplane bases, water jobs, and a runway still. A sixth more empty weight, a little slower; swap back to wheels for a fee.");
      } else {
        const int fee = career.floatSwapFee(f.spec);
        if (button(px, py, iw, 36 * s, std::string(f.fit.floats ? "Back on wheels / " : "Put on floats / ") + fmtMoney(fee), ready && career.money >= fee)) {
          std::string m; bool ok = false; const bool on = !f.fit.floats; commit([&](Career& k) { ok = k.swapFloats(oi, on, &m); }); result(m, ok);
        }
        py += 46 * s;
        note(f.fit.floats ? "The floats come off at a land field and wait here for you." : "Your floats are in store: on again at any land field.");
      }
    }
    for (int k = 0; k < KIT_COUNT; k++) {
      if (!kitFor(f.spec, k)) continue;
      if (f.fit.has(k)) { g_ren.text(px, py, 13 * s, ellipsize(std::string(kitName(k, a)) + "  -  fitted", iw, 13 * s), C_GOOD, 1); py += 22 * s; continue; }
      const int cost = career.kitPrice(f.spec, k);
      if (button(px, py, iw, 34 * s, std::string(kitName(k, a)) + " / " + fmtMoney(cost), ready && career.money >= cost)) { std::string m; bool ok = false; commit([&](Career& kc) { ok = kc.buyKit(oi, k, &m); }); result(m, ok); }
      py += 42 * s;
      note(kitBlurb(k, a));
    }
    py += 6 * s;
  }
  header(px, py, iw, "TECHNICAL DATA"); py += 28 * s;
  row("CAPACITY", fmt("%d passenger%s / %.0f kg cargo", a.pax, a.pax == 1 ? "" : "s", a.cargoKg));
  const char* et[] = {"Piston", "Turboprop", "Turbofan"};
  row("POWERPLANT", fmt("%d x %s%s", a.engines, et[a.engineType], a.engineType == ENG_PISTON ? fmt(" (%d-cyl)", a.cylinders).c_str() : ""));
  row("FUEL", fmt("%.0f kg: %.1f h at cruise", af.maxFuel, cruiseMinutes(af, af.maxFuel) / 60.f));
  row("RUNWAY AT SEA LEVEL", fmt("%.0f m / %s", af.runwayM, af.roughOK ? "gravel / snow" : af.runwayM < 900 ? "grass" : "paved"));
  row("LANDING GEAR", af.floats ? "Amphibious floats (retractable wheels)" : af.taildragger ? "Taildragger" : af.retract ? "Retractable tricycle" : "Fixed tricycle");
  if (!af.floats && floatKitFor(selHangar)) row("ON THE WATER", "Amphibious floats at the mechanic's, once it's yours");
  row("LICENCE", licenseName(a.license));
  row("RENTAL / FLIGHT", a.rentFee ? fmtMoney(a.rentFee) : "Not available");
  header(px, py, iw, "FLEET SERVICES"); py += 28 * s;
  if (career.loan.open()) note(fmt("Loan: %s remaining, %s per flight%s", fmtMoney(career.loan.balance).c_str(), fmtMoney(career.loan.payment).c_str(), career.loan.missed ? fmt(" / %d missed", career.loan.missed).c_str() : ""), C_WARN);
  if (career.fleet.empty()) note("No owned aircraft yet. Rentals are available at every airport.");
  for (size_t fi = 0; fi < career.fleet.size(); ++fi) {
    const OwnedPlane f = career.fleet[fi]; const AircraftSpec& fs = career.airframe((int)fi);
    g_ren.text(px, py, 16 * s, ellipsize(std::string(fs.name) + (f.fit.floats ? "  (floats)" : ""), iw, 16 * s), C_TEXT, 1); py += 25 * s;
    note(fmt("%s / %.0f%% fuel / %.0f%% condition%s", parkedAt(f, false).c_str(), 100 * f.fuel / std::max(fs.maxFuel, 1.f), 100 * f.condition, career.routeOf((int)fi) >= 0 ? " / on route" : ""));
    const bool atHand = career.here(f) && f.location == career.location;
    if (atHand && f.fuel < fs.maxFuel - 1) {
      const int cost = (int)((fs.maxFuel - f.fuel) * career.fuelPrice(career.location, f.spec));
      if (button(px, py, iw, 32 * s, "Fill up / " + fmtMoney(cost), !commitBlocked() && career.money >= cost)) { std::string m; bool ok = false; commit([&](Career& k) { ok = k.refuel((int)fi, &m); }); result(m, ok); } py += 42 * s;
    }
    if (atHand && f.condition < 0.97f) {
      const int cost = career.serviceCost((int)fi);
      if (button(px, py, iw, 32 * s, "Service / " + fmtMoney(cost), !commitBlocked() && career.money >= cost)) { std::string m; bool ok = false; commit([&](Career& k) { ok = k.service((int)fi, &m); }); result(m, ok); } py += 42 * s;
    }
    py += 12 * s;
  }
  if (!career.fleet.empty()) {
    if (button(px, py, iw, 34 * s, career.insured ? "Hull insurance / ON" : "Hull insurance / OFF", !commitBlocked())) { commit([](Career& k) { k.insured = !k.insured; }); hubMsg = career.insured ? "Hull insurance enabled." : "Hull insurance disabled."; hubMsgTime = 4; }
    py += 46 * s;
    int prem = 0; for (auto& f : career.fleet) prem = std::max(prem, career.insurancePremium(f.spec));
    note(fmt("Up to %s per flight. Covers repairs after a failure or belly landing.", fmtMoney(prem).c_str()));
  }
  contentH = py + scroll - regTop;
  hitClipOn = false; buttonContentOffsetY = 0; g_ren.uiClipOff();
  if (contentH > regBot - regTop) {
    clipFade(rx + 8 * s, regBot, rw - 16 * s, 24 * s, panelAt(y, h, regBot));
    const float track = regBot - regTop, thumb = std::max(24 * s, track * track / contentH), limit = contentH - track;
    g_ren.rect(rx + rw - 7 * s, regTop, 2 * s, track, C_DIM, 0.16f);
    g_ren.rect(rx + rw - 7 * s, regTop + (track - thumb) * std::min(scroll / limit, 1.f), 2 * s, thumb, C_ACCENT, 0.8f);
    g_ren.text(px, y + h - 17 * s, 10 * s, ellipsize("SCROLL / SPECIFICATIONS + FLEET SERVICES", iw, 10 * s), C_DIM, 0.8f, 0, false);
  }
  focusScrollPixels(focus0, uid(0, 0, "hangar-details-scroll"), rx, regTop, rw, regBot, scroll, inspectWheeled, contentH > regBot - regTop);
}

// Standalone, career-independent sandbox setup. Its catalog intentionally excludes research teasers.
// This screen deliberately reads only immutable request metadata and atomic stage flags.
// The worker may be replacing every byte of the resident World underneath it.
void Game::drawRegionLoading() {
  const float s = S(), W = (float)g_ren.W, H = (float)g_ren.H;
  const float x = W * .12f, w = W * .76f, y = H * .29f;
  g_ren.rectGrad(0, 0, W, H, vec3(.008f, .025f, .045f), C_PANEL, 1);
  // what is being prepared, for whom: a free flight's region, the career's (its hub, or a flight's end there), or the
  // islands again on the way back to the menu
  const char* head = regionAfterLoad == SCR_FREE_FLIGHT ? "FREE FLIGHT / REGION PREPARATION" :
    regionAfterLoad == SCR_HUB || regionAfterLoad == SCR_DEBRIEF ? "CAREER / REGION PREPARATION" : "RETURNING TO THE SOLACE ISLANDS";
  g_ren.text(x, y, 12 * s, head, C_ACCENT, 1, 0, false);
  fitText(x, y + 34 * s, w, 30 * s, 18 * s, loadingRegion.name, C_TEXT);
  // the bar: the stages' share of the work - the last region's readers drained, the build (the builder's own measure of
  // its steps, World::buildConfigured), its map and buffers made, their upload a slice a frame - and never stepping back
  const int stage = regionLoadStage.load();
  const float built = std::clamp(g_worldLoadProgress.load(), 0.f, 1.f);
  const float done = stage == 0 ? 0.f : stage == 1 ? .04f + .78f * built : stage == 2 ? .84f : .88f + .12f * g_ren.worldUploadFraction();
  regionShown = std::max(regionShown, regionShown + (done - regionShown) * 0.12f);   // (eased a frame at a time: drawn before the UI's clock)
  std::string detail = stage == 0 ? "Finishing background work from the previous region" :
    stage == 1 ? (g_worldStage.load() == 1 ? std::string("Checking the region cache") : std::string(g_worldLoadTask.load())) :
    stage == 2 ? "Preparing the region's map and its terrain for the renderer" : "Uploading terrain and refreshing the renderer";
  if (!regionError.empty()) detail = "Region preparation failed: " + regionError;
  else detail += fmt("   %.0f s", regionLoadTime);
  fitText(x, y + 92 * s, w - 60 * s, 16 * s, 11 * s, detail, regionError.empty() ? C_DIM : C_WARN);
  g_ren.rect(x, y + 131 * s, w, 4 * s, C_ACCENT, .12f, 2 * s);
  if (regionError.empty()) {
    g_ren.text(x + w, y + 106 * s, 12 * s, fmt("%d%%", std::min(99, (int)(regionShown * 100.f))), C_ACCENT, 1, 2, false);
    if (regionShown > 0.f) g_ren.rect(x, y + 131 * s, w * regionShown, 4 * s, C_ACCENT, .9f, 2 * s);
    g_ren.glow(x + w * regionShown - 4 * s, y + 129 * s, 8 * s, 8 * s, C_ACCENT, .5f + .3f * sinf(realTime * 6.f), 4 * s, 14 * s);   // (alive while one long step runs)
  }
  g_ren.text(x, y + 155 * s, 12 * s, "The next region replaces the previous one. Your career is unchanged.", C_DIM, 1);
  if (!regionError.empty()) {
    if (button(x, y + 200 * s, 190 * s, 38 * s, "Retry region")) { loadFreeRegion(freeRegion, regionAfterLoad); return; }
    if (button(x + 205 * s, y + 200 * s, 240 * s, 38 * s, "Restore Solace / Main menu")) { loadFreeRegion(0, SCR_MENU); return; }
  } else if (regionCancel) {
    g_ren.text(x, y + 205 * s, 13 * s, "Returning to Solace as soon as the current build finishes...", C_WARN, 1);
  } else if (button(x, y + 200 * s, 250 * s, 38 * s, "Cancel / Return to Solace")) regionCancel = true;
}

void Game::drawRegionPicker() {
  initFreeRegions();
  const bool flying = screen == SCR_FLIGHT && freeFlight;
  const float s = S(), W = (float)g_ren.W, H = (float)g_ren.H;
  const float x = 48 * s, w = W - 96 * s, y = 139 * s;
  const float h = std::min(w * .5f, H - 310 * s);
  g_ren.rect(0, 0, W, H, C_PANEL, .97f);
  g_ren.text(x, 24 * s, 27 * s, "EARTH DESTINATIONS", C_TEXT, 1);
  g_ren.text(x, 67 * s, 13 * s, flying ? "Select a destination to fly to. Your aircraft stays at its current Earth position." : "Click any Earth address to choose a repeatable island region.", C_DIM, 1);
  g_ren.text(x, 91 * s, 11 * s, "Invented landscapes and airports. These are not real-world coastlines.", C_WARN, 1);
  if (button(W - 225 * s, 23 * s, 177 * s, 35 * s, flying ? "Back to flight" : "Back to aircraft") || in.pressed[K_ESC]) {
    freeRegionMap = false; in.pressed[K_ESC] = false; return;
  }
  g_ren.rect(x, y, w, h, vec3(.015f, .07f, .10f), 1, 3 * s);
  for (int lon = -180; lon <= 180; lon += 30) {
    const float px = x + (lon + 180.f) / 360.f * w;
    g_ren.line(px, y, px, y + h, 1, C_ACCENT, lon == 0 ? .36f : .12f);
    if (lon % 60 == 0 && lon != 180) g_ren.text(px, y + h + 6 * s, 9 * s, fmt("%d", lon), C_DIM, 1, 1, false);   // (180's label would sit under the region buttons)
  }
  for (int lat = -90; lat <= 90; lat += 30) {
    const float py = y + (90.f - lat) / 180.f * h;
    g_ren.line(x, py, x + w, py, 1, C_ACCENT, lat == 0 ? .36f : .12f);
    g_ren.text(x - 8 * s, py - 5 * s, 9 * s, fmt("%d", lat), C_DIM, 1, 2, false);
  }
  // Every click resolves a bounded cube-cell descriptor; no global terrain allocation.
  if (hovered(x, y, w, h) && in.mPressed[0]) {
    const double longitude = std::clamp((double)(in.mx - x) / w * 360.0 - 180.0, -180.0, 180.0);
    const double latitude = std::clamp(90.0 - (double)(in.my - y) / h * 180.0, -90.0, 90.0);
    chooseFreeRegion(procedural::describe({latitude, longitude, 0}));
    g_audio.trigger(SFX_CLICK);
  }
  for (int i = 0; i < (int)freeRegions.size(); ++i) {
    const auto& r = freeRegions[i];
    const float px = x + (float)(r.origin.longitude + 180.0) / 360.f * w;
    const float py = y + (float)(90.0 - r.origin.latitude) / 180.f * h;
    const bool selected = i == freeRegion;
    g_ren.rect(px - (selected ? 5.f : 3.f) * s, py - (selected ? 5.f : 3.f) * s,
      (selected ? 10.f : 6.f) * s, (selected ? 10.f : 6.f) * s, selected ? C_WARN : C_ACCENT, 1, 3 * s);
    if (selected) g_ren.text(std::clamp(px, x + 120 * s, x + w - 120 * s), std::clamp(py - 22 * s, y + 5 * s, y + h - 30 * s), 12 * s, r.name, C_TEXT, 1, 1);
  }
  if (button(x + w - 84 * s, y + h + 4 * s, 36 * s, 25 * s, "<")) freeRegion = (freeRegion + (int)freeRegions.size() - 1) % (int)freeRegions.size();
  if (button(x + w - 36 * s, y + h + 4 * s, 36 * s, 25 * s, ">")) freeRegion = (freeRegion + 1) % (int)freeRegions.size();
  const auto& region = freeRegions[freeRegion];
  if (flying) {
    const float px = x + (float)(earthPosition.longitude + 180.) / 360.f * w;
    const float py = y + (float)(90. - earthPosition.latitude) / 180.f * h;
    g_ren.rectOutline(px - 7 * s, py - 7 * s, 14 * s, 14 * s, C_GOOD, 1, 7 * s, 2 * s);
    g_ren.text(px, py + 10 * s, 10 * s, "YOU", C_GOOD, 1, 1);
  }
  const float infoY = y + h + 32 * s;
  fitText(x, infoY, w - 245 * s, 19 * s, 12 * s, region.name, C_TEXT);
  g_ren.text(x, infoY + 28 * s, 11 * s, fmt("%+.2f latitude / %+.2f longitude / %s", region.origin.latitude,
    region.origin.longitude, region.authored ? "Original Solace" : procedural::archetypeName(region.archetype)), C_ACCENT, 1);
  const std::string routeInfo = flying && !earthPrefetchError.empty() ? "Terrain preparation failed. Set the route again to retry; flight remains available." : flying && earthFacilitiesPending ? "Local airports unavailable until detail is active. Visible coarse terrain is solid." : flying ? fmt("%.0f km to destination / nearby regions prepare as you fly", geo::surfaceDistance(earthPosition, region.origin) / 1000.) :
    "Fly across open ocean. Nearby regions prepare in the background.";
  g_ren.text(x, infoY + 51 * s, 10 * s, routeInfo, earthPrefetchError.empty() ? C_DIM : C_WARN, 1);
  if (flying && plane.spec) {
    const float range = plane.nominalRangeKm();
    const bool beyondRange = geo::surfaceDistance(earthPosition, region.origin) / 1000. > range;
    g_ren.text(x, infoY + 71 * s, 10 * s, fmt("%s / nominal range at current fuel %.0f km, no reserve%s", plane.spec->name, range,
      beyondRange ? " / ROUTE EXCEEDS RANGE" : ""), beyondRange ? C_WARN : C_DIM, 1);
  }
  if (button(x + w - 230 * s, infoY, 230 * s, 40 * s,
      flying ? (plane.onGround ? "Set departure route" : "Set route / heading hold") : region.id == g_world.regionId ? "Use this region" : "Load selected region", true, true)) {
    freeRegionMap = false;
    if (flying) setEarthDestination(region); else loadFreeRegion(freeRegion);
    return;
  }
}

// Free Flight on the real Earth: an airport chosen by typing its code or its name (or from the largest, with nothing
// typed); its chart is loaded and the flight starts there (earth_charts.h)
void Game::drawEarthAirportPicker() {
  const float s = S(), W = (float)g_ren.W, H = (float)g_ren.H;
  const float x = 48 * s, w = W - 96 * s;
  const bool flying = screen == SCR_FLIGHT && freeFlight;   // (in flight: a route to the airport chosen, not a start there)
  auto pack = earthCharts::pack();
  g_ren.rect(0, 0, W, H, C_PANEL, .97f);
  g_ren.text(x, 24 * s, 27 * s, flying ? "FLY TO A REAL AIRPORT" : "THE REAL EARTH", C_TEXT, 1);
  if (button(W - 225 * s, 23 * s, 177 * s, 35 * s, flying ? "Back to flight" : "Back to aircraft") || (in.pressed[K_ESC] && earthQuery.empty())) {
    freeEarthPicker = false; in.pressed[K_ESC] = false; return;
  }
  if (!pack) { g_ren.text(x, 70 * s, 14 * s, "No Earth pack: " + earthCharts::packError(), C_WARN, 1); return; }
  // a streamed pack: what it has kept here, and all of it for flying with no connection (earth_install.h)
  const auto* install = earthInstaller.get();
  const bool installing = install && install->state() == earthInstall::Installer::RUNNING;
  if (!flying && (pack->streamed() || install) && !diskless && !saveDir.empty()) {
    const auto st = pack->streamStats();
    const float bw = 300 * s, bx = W - 225 * s - 12 * s - bw;
    // (the source built again since this copy came: its pieces gone, the copy got again, streamed as before)
    const std::string label = installing ? fmt("Getting it %.0f%%", install->progress() * 100.f) : st.gone ? "Get it again" :
                              pack->streamed() ? fmt("Keep all of it offline (%.1f GB)", st.remote / 1e9) : "Downloaded";
    if (button(bx, 23 * s, bw, 35 * s, label, !installing && pack->streamed(), st.gone) && startEarthInstall(!st.gone)) g_audio.trigger(SFX_CLICK);
    if (installing) g_ren.rect(bx + 4 * s, 23 * s + 33 * s, (bw - 8 * s) * std::clamp(install->progress(), 0.f, 1.f), 2 * s, C_ACCENT, 0.9f);
  }
  if (install) g_ren.text(x, 67 * s, 13 * s, ellipsize(install->status(), w, 13 * s), install->state() == earthInstall::Installer::FAILED ? C_WARN : C_ACCENT, 1);
  else if (pack->streamed() && pack->streamStats().gone)
    g_ren.text(x, 67 * s, 13 * s, "The real Earth was built again since this copy came: get it again (above), and fly on.", C_WARN, 1);
  else if (flying) g_ren.text(x, 67 * s, 13 * s, fmt("The autopilot holds the course there; the charts ahead prepare as you fly. %s, %.0f km range at the fuel aboard.",
                                               plane.spec ? plane.spec->name : "", plane.spec ? plane.nominalRangeKm() : 0.f), C_DIM, 1);
  else g_ren.text(x, 67 * s, 13 * s, fmt(pack->streamed() ? "Real terrain, land cover, airports, towns and roads: %zu airports, the land streamed as you fly (%.0f MB kept here). Type a code or a name." :
                                                         "Real terrain, coasts, land cover, airports, towns and roads from the Earth pack: %zu airports. Type a code or a name.",
                                   pack->airportCount(), pack->streamStats().kept / 1e6), C_DIM, 1);
  // the query: typed, Backspace takes a letter off, Esc clears it
  for (char ch : in.text) if (earthQuery.size() < 40 && ch >= 32 && ch < 127) earthQuery += ch;
  if (in.pressed[K_BACK] && !earthQuery.empty()) earthQuery.pop_back();
  if (in.pressed[K_ESC] && !earthQuery.empty()) { earthQuery.clear(); in.pressed[K_ESC] = false; }
  if (earthResultsFor != earthQuery) { earthResults = earthCharts::searchAirports(earthQuery, 40); earthResultsFor = earthQuery; earthPick = 0; }
  const float by = 100 * s;
  g_ren.rect(x, by, std::min(w, 620 * s), 40 * s, C_PANEL2, .95f, 4 * s);
  g_ren.rectOutline(x, by, std::min(w, 620 * s), 40 * s, C_ACCENT, .7f, 4 * s, 1.5f * s);
  const bool caret = fmodf(realTime, 1.f) < .55f;
  g_ren.text(x + 14 * s, by + 10 * s, 18 * s, earthQuery.empty() ? std::string(caret ? "|" : "") + "  (the largest airports)" : earthQuery + (caret ? "|" : ""),
             earthQuery.empty() ? C_DIM : C_TEXT, 1);
  const float ly = by + 58 * s, step = 44 * s;
  const int visible = std::max(1, (int)((H - ly - 30 * s) / step)), n = (int)earthResults.size();
  if (in.pressed[K_DOWN]) earthPick = std::min(earthPick + 1, std::max(0, n - 1));
  if (in.pressed[K_UP]) earthPick = std::max(earthPick - 1, 0);
  const int first = std::clamp(earthPick - visible + 1, 0, std::max(0, n - visible));
  if (n == 0) g_ren.text(x, ly, 14 * s, "No airport matches.", C_DIM, 1);
  for (int i = first; i < std::min(n, first + visible); ++i) {
    const auto& a = earthResults[i];
    const float ry = ly + (i - first) * step;
    const bool hov = hovered(x, ry, w, step - 6 * s);
    card(x, ry, w, step - 6 * s, i == earthPick, hov, C_ACCENT);
    if (hov && in.mPressed[0]) { earthPick = i; chooseEarthAirport(a); return; }
    static const char* kinds[] = {"Airfield", "Regional", "International"};
    const float len = a.runways.empty() ? 0.f : a.runways[0].length;
    g_ren.text(x + 12 * s, ry + 9 * s, 15 * s, a.ident, C_ACCENT, 1);
    g_ren.text(x + 110 * s, ry + 6 * s, 14 * s, ellipsize(a.name, w * .55f, 14 * s), C_TEXT, 1);
    g_ren.text(x + 110 * s, ry + 24 * s, 11 * s, ellipsize(a.municipality + (a.country.empty() ? "" : ", " + a.country), w * .55f, 11 * s), C_DIM, 1);
    if (flying) g_ren.text(x + w - 16 * s, ry + 12 * s, 12 * s, fmt("%s / %.0f m / %.0f km", kinds[std::clamp(a.type, 0, 2)], len,
                                     geo::surfaceDistance(earthPosition, {a.lat, a.lon, 0}) / 1000.), C_DIM, 1, 2);
    else g_ren.text(x + w - 16 * s, ry + 12 * s, 12 * s, fmt("%s / %.0f m", kinds[std::clamp(a.type, 0, 2)], len), C_DIM, 1, 2);
  }
  if ((in.pressed[K_ENTER] || (in.buttonsPressed & PAD_A)) && earthPick < n) { in.pressed[K_ENTER] = false; chooseEarthAirport(earthResults[earthPick]); }
}

void Game::chooseEarthAirport(const earth::AirportRecord& airport) {
  if (screen == SCR_FLIGHT && freeFlight) {   // (in flight: the route there)
    g_audio.trigger(SFX_CLICK); freeEarthPicker = false; paused = false;
    setEarthAirportRoute(airport);
    return;
  }
  const auto chart = earthCharts::chartForAirport(airport);
  g_audio.trigger(SFX_CLICK);
  freeEarthPicker = false; freeEarthIdent = airport.ident;
  chooseFreeRegion(chart);
  loadFreeRegion(freeRegion);
}

void Game::drawFreeFlightSetup(const FrameParams& fp) {
  (void)fp;
  initFreeRegions();
  if (freeRegionMap) { drawRegionPicker(); return; }
  if (freeEarthPicker) { drawEarthAirportPicker(); return; }
  const float s = S(), W = (float)g_ren.W, H = (float)g_ren.H;
  const float x = 24 * s, y = 126 * s, w = W - 48 * s, h = H - 146 * s;
  const bool realEarth = earthCharts::pack() != nullptr;
  const HangarLayout L = hangarLayout(x, y, w, h, s);
  const float lw = L.leftWidth, rx = L.rightX, rw = L.rightWidth;
  freeCraft = validCareerSelection(freeCraft);
  if (!demoCraft(freeCraft)) freeCraft = 0;
  const int airportCount = (int)g_world.airports.size();
  if (airportCount == 0) { if (button(x, y, 260 * s, 42 * s, "Back to main menu")) cancelFreeFlightSetup(); return; }
  freeAirport = std::clamp(freeAirport, 0, airportCount - 1);
  uiGlass = true;
  g_ren.rectGrad(0, 0, W, 110 * s, C_PANEL, vec3(0.007f, 0.018f, 0.032f), 0.96f);
  g_ren.text(x, 22 * s, 28 * s, "FREE FLIGHT", C_TEXT, 1);
  // the real Earth not there: downloaded from here (earth_install.h), its progress where the screen's line goes
  const auto* install = earthInstaller.get();
  const bool offerInstall = !realEarth && !diskless && !saveDir.empty();
  if (install) {
    const bool failed = install->state() == earthInstall::Installer::FAILED;
    g_ren.text(x, 61 * s, 14 * s, ellipsize(install->status(), W - 48 * s, 14 * s), failed ? C_WARN : C_ACCENT, 1);
  } else {
    g_ren.text(x, 61 * s, 14 * s, ellipsize(realEarth ? "Fly Solace, the real Earth or generated destinations. No fees, missions or career changes." :
               offerInstall ? "Fly Solace or generated Earth destinations. No fees, missions or career changes. The real Earth: get it here, its land streamed as you fly." :
               "Fly Solace or generated Earth destinations. No fees, missions or career changes. (The real Earth: its pack in an earth folder beside the game.)", W - 48 * s, 14 * s), C_DIM, 1);
  }
  const float selectW = std::max(240 * s, W - (realEarth || offerInstall ? 800 : 490) * s);
  if (button(x, 86 * s, 30 * s, 28 * s, "<")) freeRegion = (freeRegion + (int)freeRegions.size() - 1) % (int)freeRegions.size();
  if (button(x + selectW - 30 * s, 86 * s, 30 * s, 28 * s, ">")) freeRegion = (freeRegion + 1) % (int)freeRegions.size();
  const auto& region = freeRegions[freeRegion];
  const bool resident = region.id == g_world.regionId;
  g_ren.text(x + 40 * s, 89 * s, 13 * s, ellipsize(region.name, selectW - 80 * s, 13 * s), resident ? C_ACCENT : C_WARN, 1);
  if (button(x + selectW + 12 * s, 86 * s, 150 * s, 28 * s, resident ? "Region loaded" : "Load region", !resident, !resident)) {
    loadFreeRegion(freeRegion); uiGlass = false; return;
  }
  if (button(x + selectW + 174 * s, 86 * s, 180 * s, 28 * s, "Choose on Earth map")) { freeRegionMap = true; uiGlass = false; return; }
  if (realEarth && button(x + selectW + 366 * s, 86 * s, 138 * s, 28 * s, "Real Earth", true, earthCharts::isEarthId(region.id))) {
    freeEarthPicker = true; earthPick = 0; uiGlass = false; return;
  }
  if (offerInstall) {
    const bool running = install && install->state() == earthInstall::Installer::RUNNING;
    const float bx = x + selectW + 366 * s, bw = 300 * s;
    const std::string label = running ? fmt("Getting the real Earth %.0f%%", install->progress() * 100.f) :
                              install ? "Try again" : "Get the real Earth";
    if (button(bx, 86 * s, bw, 28 * s, label, !running, !install) && startEarthInstall()) g_audio.trigger(SFX_CLICK);
    if (running) g_ren.rect(bx + 4 * s, 110 * s, (bw - 8 * s) * std::clamp(install->progress(), 0.f, 1.f), 2 * s, C_ACCENT, 0.9f);
  }
  // what lies beyond the islands: the real Earth (Bermuda 640 km west, the American coast beyond) or the generated islands
  if (realEarth && button(x + selectW + 516 * s, 86 * s, 150 * s, 28 * s, freeGeneratedEarth ? "Beyond: generated" : "Beyond: real Earth",
                          region.id == "solace", region.id == "solace" && !freeGeneratedEarth)) {
    freeGeneratedEarth = !freeGeneratedEarth; g_audio.trigger(SFX_CLICK);
  }
  if (button(W - 220 * s, 25 * s, 196 * s, 38 * s, "Back to main menu")) { cancelFreeFlightSetup(); uiGlass = false; return; }
  panel(x, y, lw, h); panel(rx, y, rw, h);
  g_ren.text(x + 18 * s, y + 18 * s, 22 * s, "Aircraft", C_TEXT, 1);
  g_ren.text(x + 18 * s, y + 49 * s, 11 * s, demo ? fmt("3 OF %d IN THE DEMO / FREE", kNumAircraft) : fmt("ALL %d AVAILABLE / FREE", kNumAircraft), C_ACCENT, 1, 0, false);
  const float craftTop = y + 78 * s, craftBottom = y + h - 30 * s, craftStep = 68 * s;
  const int craftVisible = std::max(1, (int)((craftBottom - craftTop) / craftStep));
  static int craftFirst = 0, craftSeen = -1, airportFirst = 0, airportSeen = -1;
  auto craftId = [&](int i) { return uid(0, (float)i, "free-flight-airframe"); };
  bool craftWheeled = false, airportWheeled = false;
  if (hovered(x, craftTop, lw, craftBottom - craftTop) && in.wheel) { craftFirst -= (int)in.wheel; in.wheel = 0; craftWheeled = true; }
  if (craftSeen != freeCraft) { const int row = careerRowFor(freeCraft); if (row < craftFirst) craftFirst = row; if (row >= craftFirst + craftVisible) craftFirst = row - craftVisible + 1; craftSeen = freeCraft; }
  focusScrollRows([&](int i) { return craftId(careerSpecAt(i)); }, kNumAircraft, craftFirst, craftVisible, 1, craftWheeled);
  craftFirst = std::clamp(craftFirst, 0, std::max(0, kNumAircraft - craftVisible));
  for (int i = 0; i < kNumAircraft; ++i) {
    const int spec = careerSpecAt(i);
    const float cy = craftTop + (i - craftFirst) * craftStep;
    focusList.push_back({craftId(spec), x + 10 * s, cy, lw - 20 * s, craftStep - 8 * s});
    if (i < craftFirst || i >= craftFirst + craftVisible) continue;
    const bool focused = focusNav && focusId == craftId(spec), hov = hovered(x + 10 * s, cy, lw - 20 * s, craftStep - 8 * s);
    card(x + 10 * s, cy, lw - 20 * s, craftStep - 8 * s, freeCraft == spec, hov || focused, C_ACCENT);
    if (focused) g_ren.rectOutline(x + 8 * s, cy - 2 * s, lw - 16 * s, craftStep - 4 * s, C_ACCENT, 0.85f, 5 * s, 1.5f * s);
    const bool activate = focused && (in.pressed[K_ENTER] || in.pressed[' ']);
    const bool locked = !demoCraft(spec);   // (the demo: the rest of the fleet shown, in the full game)
    if (((hov && in.mPressed[0]) || activate) && !locked) { freeCraft = spec; g_audio.trigger(SFX_CLICK); if (activate) in.pressed[K_ENTER] = in.pressed[' '] = false; }
    g_ren.text(x + 23 * s, cy + 10 * s, 16 * s, ellipsize(kAircraft[spec].name, lw - 46 * s, 16 * s), locked ? C_DIM : C_TEXT, 1);
    g_ren.text(x + 23 * s, cy + 35 * s, 11 * s, ellipsize(locked ? std::string("In the full game") : std::string(kAircraft[spec].role), lw - 46 * s, 11 * s), locked ? C_ACCENT : C_DIM, 1);
  }
  g_ren.text(x + 18 * s, y + h - 22 * s, 11 * s, ellipsize(fmt("%02d - %02d / %02d   SCROLL", craftFirst + 1, std::min(kNumAircraft, craftFirst + craftVisible), kNumAircraft), lw - 36 * s, 11 * s), C_DIM, 0.85f, 0, false);
  if (!floatKitFor(freeCraft)) freeFloats = false;
  const AircraftSpec& spec = airframeFor(freeCraft, Career::SRC_NONE);   // (on floats, if chosen)
  hangarView.select(freeCraft, true);
  const float vx = L.previewX, vw = L.previewWidth;
  g_ren.rectGrad(vx, y, vw, 82 * s, C_PANEL, C_PANEL2, 0.86f, 4 * s);
  g_ren.text(vx + 18 * s, y + 13 * s, 11 * s, "READY FOR FREE FLIGHT", C_ACCENT, 1, 0, false);
  // (the types that take floats: on wheels or on amphibious floats - seaplane bases, and a runway still)
  if (floatKitFor(freeCraft) && button(vx + vw - 178 * s, y + 8 * s, 164 * s, 26 * s, freeFloats ? "On floats" : "On wheels", true, freeFloats)) { freeFloats = !freeFloats; g_audio.trigger(SFX_CLICK); }
  fitText(vx + 18 * s, y + 33 * s, vw - 36 * s, 28 * s, 16 * s, std::string(spec.name) + (spec.floats && !kAircraft[freeCraft].floats ? " on floats" : ""), C_TEXT);
  g_ren.text(vx + 18 * s, y + 66 * s, 10 * s, ellipsize(fmt("CRUISE / RANGE   %s / %.0f km", fmtSpeed(spec.cruise).c_str(), spec.rangeKm), vw - 36 * s, 10 * s), C_DIM, 1, 0, false);
  drawHangarPreviewControls(vx, y + h - 56 * s, vw);
  const float px = rx + 18 * s, iw = rw - 36 * s;
  if (!resident) {
    auto wrapText = [&](float tx, float ty, float width, float size, const std::string& text, vec3 color) {
      for (const auto& line : wrap(text, width, size)) { g_ren.text(tx, ty, size, line, color, 1); ty += size * 1.45f; }
    };
    header(px, y + 20 * s, iw, "DESTINATION SELECTED");
    wrapText(px, y + 58 * s, iw, 14 * s, region.name, C_TEXT);
    wrapText(px, y + 120 * s, iw, 12 * s, "Load this region to generate its terrain and choose a starting airport.", C_DIM);
    wrapText(px, y + 210 * s, iw, 11 * s, region.authored ? "Original Solace airports and career geography." :
      fmt("%+.2f / %+.2f\n%s\nGenerator v%u / %s", region.origin.latitude, region.origin.longitude,
        procedural::archetypeName(region.archetype), region.generatorVersion, region.id.c_str()), C_ACCENT);
    uiGlass = false;
    if (in.pressed[K_ESC]) cancelFreeFlightSetup();
    return;
  }
  header(px, y + 20 * s, iw, "STARTING AIRPORT");
  const float airportTop = y + 54 * s, airportBottom = y + h - 200 * s, airportStep = 49 * s;
  const int airportVisible = std::max(1, (int)((airportBottom - airportTop) / airportStep));
  auto airportId = [&](int i) { return uid(0, (float)i, "free-flight-airport"); };
  if (hovered(rx, airportTop, rw, airportBottom - airportTop) && in.wheel) { airportFirst -= (int)in.wheel; in.wheel = 0; airportWheeled = true; }
  if (airportSeen != freeAirport) { if (freeAirport < airportFirst) airportFirst = freeAirport; if (freeAirport >= airportFirst + airportVisible) airportFirst = freeAirport - airportVisible + 1; airportSeen = freeAirport; }
  focusScrollRows(airportId, airportCount, airportFirst, airportVisible, 1, airportWheeled);
  airportFirst = std::clamp(airportFirst, 0, std::max(0, airportCount - airportVisible));
  for (int i = 0; i < airportCount; ++i) {
    const float ay = airportTop + (i - airportFirst) * airportStep;
    focusList.push_back({airportId(i), px, ay, iw, airportStep - 6 * s});
    if (i < airportFirst || i >= airportFirst + airportVisible) continue;
    const Airport& airport = g_world.airports[i];
    const bool focused = focusNav && focusId == airportId(i), hov = hovered(px, ay, iw, airportStep - 6 * s);
    card(px, ay, iw, airportStep - 6 * s, freeAirport == i, hov || focused, C_ACCENT);
    if (focused) g_ren.rectOutline(px - 2 * s, ay - 2 * s, iw + 4 * s, airportStep - 2 * s, C_ACCENT, 0.85f, 5 * s, 1.5f * s);
    const bool activate = focused && (in.pressed[K_ENTER] || in.pressed[' ']);
    if ((hov && in.mPressed[0]) || activate) { freeAirport = i; g_audio.trigger(SFX_CLICK); if (activate) in.pressed[K_ENTER] = in.pressed[' '] = false; }
    g_ren.text(px + 10 * s, ay + 7 * s, 13 * s, airport.code, C_ACCENT, 1);
    if (airport.water()) g_ren.text(px + iw - 10 * s, ay + 8 * s, 10 * s, "SEAPLANE BASE", C_DIM, 0.9f, 2);
    g_ren.text(px + 10 * s, ay + 25 * s, 11 * s, ellipsize(airport.name, iw - 20 * s, 11 * s), C_TEXT, 1);
  }
  g_ren.text(px, airportBottom + 2 * s, 10 * s, fmt("%02d - %02d / %02d   SCROLL", airportFirst + 1, std::min(airportCount, airportFirst + airportVisible), airportCount), C_DIM, 0.85f, 0, false);
  const Airport& airport = g_world.airports[freeAirport];
  const float setupY = y + h - 169 * s;
  header(px, setupY, iw, "START POSITION");
  const float half = (iw - 10 * s) * 0.5f;
  const bool noStand = freeStartsAirborne();   // (a landplane at a seaplane base: it starts over the lane)
  if (button(px, setupY + 26 * s, half, 34 * s, airport.water() ? "On the water" : "Runway", !noStand, !freeAirborne && !noStand)) freeAirborne = false;
  if (button(px + half + 10 * s, setupY + 26 * s, half, 34 * s, "Airborne", true, freeAirborne || noStand)) freeAirborne = true;
  g_ren.text(px, setupY + 73 * s, 12 * s, ellipsize(std::string(airport.code) + " / " + airport.name, iw, 12 * s), C_TEXT, 1);
  const bool shortRunway = !freeAirborne && !airport.water() && airport.length < spec.runwayM;
  const std::string info = airport.water() ? fmt("%.0f m water lane / %s", airport.length, noStand ? "Seaplane base: floats only" : "Seaplane base")
                         : shortRunway ? fmt("%.0f m / Short runway: try airborne", airport.length) : fmt("%.0f m runway / %s", airport.length, surfaceName(airport.surface));
  g_ren.text(px, setupY + 93 * s, 11 * s, ellipsize(info, iw, 11 * s), shortRunway || noStand ? C_WARN : C_DIM, 1);
  if (button(px, y + h - 52 * s, iw, 36 * s, "Take flight", true, true)) { launchFreeFlight(); uiGlass = false; return; }
  uiGlass = false;
  if (in.pressed[K_ESC]) { in.pressed[K_ESC] = false; cancelFreeFlightSetup(); }
}

// ------------------------------------------------------------------ the airline tab (C11)
void Game::drawHubAirline(float x, float y, float w, float h) {
  float s = S();
  if (!career.atHome()) {   // (the airline is the islands': away, its routes fly on and pay as you fly)
    const float pw = std::min(w, 760 * s);
    panel(x, y, pw, 190 * s);
    g_ren.text(x + 24 * s, y + 20 * s, 22 * s, "Your airline flies in the Solace Islands", C_TEXT, 1);
    float py = y + 62 * s;
    const std::string t = career.airline.routes.empty()
      ? "The airline opens with your Airline Transport licence, from the islands. Travel home to put your aircraft on routes and hire their pilots."
      : fmt("Its %d route%s keep%s flying while you are away, and pay into your bank as you fly here. Travel home to change them, hire pilots or recall an aircraft.",
            (int)career.airline.routes.size(), career.airline.routes.size() == 1 ? "" : "s", career.airline.routes.size() == 1 ? "s" : "");
    for (auto& l : wrap(t, pw - 48 * s, 15 * s)) { g_ren.text(x + 24 * s, py, 15 * s, l, C_DIM, 1); py += 21 * s; }
    return;
  }
  float lw = std::min(w * 0.5f, 620 * s);
  panel(x, y, lw, h); panel(x + lw + 16 * s, y, w - lw - 16 * s, h);
  static float airScroll[2] = {0, 0}, airContent[2] = {0, 0};
  int activeColumn = 0; float clipX = x, clipW = lw; size_t focusStart = 0; bool airWheeled[2] = {false, false};
  const float clipTop = y + 16 * s, clipBottom = y + h - 24 * s;
  auto beginColumn = [&](int col, float left, float width) {
    activeColumn = col; clipX = left; clipW = width;
    float limit = std::max(0.f, airContent[col] - (clipBottom - clipTop));
    if (hovered(left, clipTop, width, clipBottom - clipTop) && in.wheel) { airScroll[col] -= in.wheel * 48 * s; in.wheel = 0; airWheeled[col] = true; }
    airScroll[col] = std::clamp(airScroll[col], 0.f, limit);
    buttonContentOffsetY = airScroll[col];
    focusStart = focusList.size();
    hitClipOn = true; hitClip[0] = left; hitClip[1] = clipTop; hitClip[2] = left + width; hitClip[3] = clipBottom;
    g_ren.uiClip(left + 8 * s, clipTop, left + width - 8 * s, clipBottom);
    return clipTop - airScroll[col];
  };
  auto endColumn = [&](float end) {
    int col = activeColumn;
    airContent[col] = end + airScroll[col] - clipTop;
    hitClipOn = false; buttonContentOffsetY = 0; g_ren.uiClipOff();
    float track = clipBottom - clipTop, limit = std::max(0.f, airContent[col] - track);
    if (limit > 0) {
      clipFade(clipX + 8 * s, clipBottom, clipW - 16 * s, 20 * s, panelAt(y, h, clipBottom));
      float thumb = std::max(24 * s, track * track / airContent[col]);
      g_ren.rect(clipX + clipW - 7 * s, clipTop, 2 * s, track, C_DIM, 0.16f);
      g_ren.rect(clipX + clipW - 7 * s, clipTop + (track - thumb) * std::min(airScroll[col] / limit, 1.f), 2 * s, thumb, C_ACCENT, 0.8f);
    }
    focusScrollPixels(focusStart, uid(0, (float)col, "airline-scroll"), clipX, clipTop, clipW, clipBottom, airScroll[col], airWheeled[col], limit > 0);
  };
  float px = x + 20 * s, py = beginColumn(0, x, lw), iw = lw - 40 * s;
  header(px, py, iw, "YOUR ROUTES"); py += 28 * s;
  if (!career.airlineOpen()) {
    for (auto& l : wrap("The airline opens with your Airline Transport licence (chapter 5 of the story). Then your own aircraft fly routes with hired pilots while you fly your own work: every flight you settle is a day of theirs.", iw, 15 * s)) { g_ren.text(px, py, 15 * s, l, C_DIM, 1); py += 21 * s; }
    endColumn(py);
    float lockX = x + lw + 40 * s, lockW = w - lw - 64 * s, lockY = y + 42 * s;
    g_ren.text(lockX, lockY, 12 * s, "NEXT CAREER MILESTONE", C_ACCENT, 1, 0, false);
    fitText(lockX, lockY + 32 * s, lockW, 28 * s, 18 * s, "Airline operations", C_TEXT);
    g_ren.text(lockX, lockY + 77 * s, 15 * s, "Earn your ATP licence to unlock", C_DIM, 1);
    g_ren.text(lockX, lockY + 101 * s, 15 * s, "routes, hired pilots and fleet income.", C_DIM, 1);
    if (button(lockX, lockY + 148 * s, std::min(lockW, 260 * s), 40 * s, "View career progress", true, true)) hubTab = TAB_LOGBOOK;
    return;
  }
  // (every line held to its panel's width: wrapped, or cut short of the buttons beside it)
  for (auto& l : wrap(fmt("Earned so far %s   -   %d incident%s   -   %d pilot%s on the payroll", fmtMoney(career.airline.earned).c_str(), career.airline.incidents, career.airline.incidents == 1 ? "" : "s", (int)career.airline.pilots.size(), career.airline.pilots.size() == 1 ? "" : "s"), iw, 14 * s)) { g_ren.text(px, py, 14 * s, l, C_DIM, 1); py += 20 * s; }
  py += 6 * s;
  if (career.airline.routes.empty()) { for (auto& l : wrap("No routes yet. Set one up on the right: an aircraft of yours, a destination, a pilot.", iw, 15 * s)) { g_ren.text(px, py, 15 * s, l, C_DIM, 1); py += 21 * s; } py += 3 * s; }
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
  for (int i = (int)career.airline.log.size() - 1; i >= std::max(0, (int)career.airline.log.size() - 12); i--) { g_ren.text(px, py, 13 * s, ellipsize(career.airline.log[i], iw, 13 * s), C_WARN, 1); py += 18 * s; }
  endColumn(py);
  // ---- right: set up a route, the pilots
  float rx = x + lw + 36 * s, ry = beginColumn(1, x + lw + 16 * s, w - lw - 16 * s), rw = w - lw - 16 * s - 40 * s;
  header(rx, ry, rw, "SET UP A ROUTE"); ry += 30 * s;
  std::vector<int> freePlanes; for (size_t fi = 0; fi < career.fleet.size(); fi++) if (career.routeOf((int)fi) < 0) freePlanes.push_back((int)fi);
  if (freePlanes.empty()) { for (auto& l : wrap("Every aircraft you own is on a route (or you own none). Buy another in the Hangar.", rw, 15 * s)) { g_ren.text(rx, ry, 15 * s, l, C_DIM, 1); ry += 21 * s; } ry += 3 * s; }
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
      for (auto& l : wrap(fmt("About %s a flight: fares %s, fuel %s, wage %s", fmtMoney(career.routeRevenue(prev) - career.routeFuelCost(prev) - p.wage).c_str(), fmtMoney(career.routeRevenue(prev)).c_str(), fmtMoney(career.routeFuelCost(prev)).c_str(), fmtMoney(p.wage).c_str()), rw, 13 * s)) { g_ren.text(rx, ry, 13 * s, l, C_DIM, 1); ry += 18 * s; }
      ry += 6 * s;
      if (button(rx, ry, 200 * s, 36 * s, "Assign route", okRoute && !commitBlocked(), okRoute)) { std::string m; bool ok = false; int pi = freePilots[airSelPilot]; commit([&](Career& k) { ok = k.assignRoute(fi, airSelDest, pi, &m); }); if (ok) g_audio.trigger(SFX_CASH); hubMsg = m; hubMsgTime = 5; }
      ry += 46 * s;
    }
  }
  ry += 10 * s;
  header(rx, ry, rw, "PILOTS"); ry += 28 * s;
  for (size_t pi = 0; pi < career.airline.pilots.size(); pi++) {
    const Career::Pilot& p = career.airline.pilots[pi];
    bool busy = false; for (auto& r : career.airline.routes) if (r.pilot == (int)pi) busy = true;
    g_ren.text(rx, ry + 4 * s, 14 * s, ellipsize(fmt("%s   rating %d   %s a flight%s", p.name.c_str(), p.rating, fmtMoney(p.wage).c_str(), busy ? "   (on a route)" : ""), rw - (busy ? 0.f : 100 * s), 14 * s), C_TEXT, 1);
    if (!busy && button(rx + rw - 90 * s, ry, 90 * s, 26 * s, "Let go", !commitBlocked(), false)) { std::string m; commit([&](Career& k) { k.firePilot((int)pi, &m); }); hubMsg = m; hubMsgTime = 4; }
    ry += 30 * s;
  }
  g_ren.text(rx, ry + 4 * s, 12 * s, "FOR HIRE", C_DIM, 1, 0, false); ry += 22 * s;
  auto cands = career.pilotCandidates();
  const std::vector<std::string> note = wrap("Rating 3 fills the cabin and rarely bends anything; rating 1 is cheap and has incidents. Insurance covers their repairs too.", rw, 12 * s);
  for (size_t ci = 0; ci < cands.size(); ci++) {
    const Career::Pilot& p = cands[ci];
    g_ren.text(rx, ry + 4 * s, 14 * s, ellipsize(fmt("%s   rating %d   %s a flight", p.name.c_str(), p.rating, fmtMoney(p.wage).c_str()), rw - 100 * s, 14 * s), C_DIM, 1);
    if (button(rx + rw - 90 * s, ry, 90 * s, 26 * s, "Hire", !commitBlocked() && career.airline.pilots.size() < 6, false)) { std::string m; Career::Pilot cp = p; commit([&](Career& k) { k.hirePilot(cp, &m); }); hubMsg = m; hubMsgTime = 4; }
    ry += 30 * s;
  }
  float ny = ry + 6 * s;
  for (auto& l : note) { g_ren.text(rx, ny, 12 * s, l, C_DIM, 1); ny += 17 * s; }
  endColumn(ny + 8 * s);
}

void Game::drawHubLogbook(float x, float y, float w, float h) {
  const float s = S(), lw = std::min(w * 0.36f, 430 * s), dx = x + lw + 16 * s, dw = w - lw - 16 * s;
  panel(x, y, lw, h); panel(dx, y, dw, h);
  fitText(x + 24 * s, y + 20 * s, lw - 48 * s, 24 * s, 15 * s, profile.name.empty() ? std::string("Pilot logbook") : "Captain " + profile.name, C_TEXT);
  // the right column's pages: the campaign, the achievements, the passport (airports, landmarks, sightings)
  {
    const char* pages[] = {"PROGRESS", "ACHIEVEMENTS", "PASSPORT"};
    const float bw = std::min(150 * s, (dw - 48 * s - 16 * s) / 3.f);
    for (int i = 0; i < 3; i++)
      if (button(dx + 24 * s + i * (bw + 8 * s), y + 18 * s, bw, 30 * s, pages[i], true, logPage == i) && logPage != i) { logPage = i; g_audio.trigger(SFX_CLICK); }
  }
  static float scroll[2] = {0, 0}, total[2] = {0, 0};
  static int pageWas = -1;
  if (pageWas != logPage) { pageWas = logPage; scroll[1] = 0; }
  const float top = y + 72 * s, bottom = y + h - 20 * s;
  for (int column = 0; column < 2; ++column) {
    float bx = column ? dx : x, bw = column ? dw : lw, px = bx + 24 * s, iw = bw - 48 * s;
    bool wheeled = false;
    if (hovered(bx, top, bw, bottom - top) && in.wheel) { scroll[column] -= in.wheel * 48 * s; in.wheel = 0; wheeled = true; }
    scroll[column] = std::clamp(scroll[column], 0.f, std::max(0.f, total[column] - (bottom - top)));
    const size_t columnFocus0 = focusList.size();
    g_ren.uiClip(bx + 12 * s, top, bx + bw - 12 * s, bottom);
    float py = top - scroll[column];
    if (!column) {
      const float half = (iw - 12 * s) * 0.5f;
      const std::string values[] = {fmt("%d", career.flights), fmt("%.1f h", career.hours), fmt("%d", career.landings), fmt("%d", career.crashes), fmt("%d", career.reputation), fmt("%d", (int)career.fleet.size())};
      const char* labels[] = {"FLIGHTS", "FLIGHT TIME", "LANDINGS", "ACCIDENTS", "REPUTATION", "AIRCRAFT"};
      for (int i = 0; i < 6; ++i) {
        float mx = px + (i % 2) * (half + 12 * s), my = py + (i / 2) * 78 * s;
        g_ren.rect(mx, my, half, 66 * s, C_PANEL2, 0.65f, 4 * s);
        g_ren.text(mx + 12 * s, my + 10 * s, 10 * s, labels[i], C_DIM, 1, 0, false);
        g_ren.text(mx + 12 * s, my + 31 * s, 22 * s, values[i], C_TEXT, 1);
      }
      py += 244 * s;
      g_ren.text(px, py, 12 * s, "SOFTEST LANDING", C_DIM, 1, 0, false);
      g_ren.text(px + iw, py, 15 * s, career.bestLandingFpm < 9000 ? fmt("%.0f fpm", career.bestLandingFpm) : "No landings yet", C_TEXT, 1, 2); py += 40 * s;
      if (!profile.best.empty()) {   // each type's softest, from the profile (every career, every flight)
        header(px, py, iw, "SOFTEST BY TYPE"); py += 30 * s;
        for (int i = 0; i < kAircraftCount; i++) {
          auto it = profile.best.find(kAircraft[i].id);
          if (it == profile.best.end()) continue;
          g_ren.text(px, py, 14 * s, ellipsize(kAircraft[i].name, iw - 110 * s, 14 * s), C_TEXT, 1);
          g_ren.text(px + iw, py, 14 * s, fmt("%.0f fpm", it->second), touchdownGrade(it->second) == TD_BUTTER ? vec3(1.f, 0.85f, 0.45f) : C_TEXT, 1, 2);
          py += 22 * s;
        }
        py += 18 * s;
      }
      header(px, py, iw, "LICENCES"); py += 30 * s;
      const char* unlocks[] = {"Dawnlark trainer / flight school", "Dawnlark and Ambler rentals / cargo work", "Scrubrunner, Skerry and Longshore / passengers / ownership", "Archipel and Emissary / airline operations"};
      for (int i = 0; i < 4; ++i) {
        bool got = career.license >= i;
        g_ren.rect(px, py + 4 * s, 7 * s, 7 * s, got ? C_GOOD : C_DIM, got ? 1.f : 0.4f, 2 * s);
        g_ren.text(px + 18 * s, py, 15 * s, licenseName(i), got ? C_TEXT : C_DIM, 1); py += 23 * s;
        for (auto& line : wrap(unlocks[i], iw - 18 * s, 12 * s)) { g_ren.text(px + 18 * s, py, 12 * s, line, C_DIM, 0.9f); py += 17 * s; }
        py += 20 * s;
      }
    } else if (logPage == 1) {   // the achievements: earned ones lit, with the day; the rest with how far along
      g_ren.text(px, py, 12 * s, fmt("%02d / %02d ACHIEVEMENTS", (int)profile.ach.size(), kAchievementCount), C_ACCENT, 1, 0, false); py += 26 * s;
      g_ren.rect(px, py, iw, 4 * s, C_DIM, 0.2f, 2 * s);
      g_ren.rect(px, py, std::max(2 * s, iw * profile.ach.size() / (float)kAchievementCount), 4 * s, C_ACCENT, 1, 2 * s); py += 22 * s;
      const int nAp = (int)profile.airports.size(), nSites = (int)profile.sites.size(), islandSites = profile.islandSites();
      if (!g_world.procedural) profileSites();   // (the islands' count, below)
      auto progress = [&](const std::string& id) -> std::string {
        if (id == "butter25") return fmt("%d / 25", (int)profile.count("butters"));
        if (id == "airports10") return fmt("%d / 10", std::min(nAp, 10));
        if (id == "airports_all") return fmt("%d / %d", profile.islandStamps(false), islandFields(false));
        if (id == "harbours_all") return fmt("%d / %d", profile.islandStamps(true), islandFields(true));
        if (id == "sites5") return fmt("%d / 5", std::min(nSites, 5));
        if (id == "sites_all") return solaceSiteCount() > 0 ? fmt("%d / %d", islandSites, solaceSiteCount()) : fmt("%d found", islandSites);
        if (id == "regions5") return fmt("%d / 5", std::min((int)profile.regionsLanded().size(), 5));
        if (id == "discover20") return fmt("%d / 20", std::min(profile.sitesBeyond(), 20));
        if (id == "region_all") {   // the region nearest done
          int best = 0, of = 0;
          for (const std::string& r : profile.regionsLanded()) {
            const int got = profile.regionAirports(r), all = regionAirportCount(r);
            if (all > 0 && (of == 0 || got * of > best * all)) { best = got; of = all; }
          }
          return of ? fmt("%d / %d", best, of) : "";
        }
        if (id == "warp") return fmt("%.0f / 500 km", std::min(profile.count("warpKm"), 500.f));
        if (id == "fleet3") return fmt("%d / 3", std::min((int)career.fleet.size(), 3));
        return "";
      };
      for (int i = 0; i < kAchievementCount; i++) {
        const Achievement& ac = kAchievements[i];
        auto it = profile.ach.find(ac.id);
        const bool got = it != profile.ach.end();
        const vec3 gold(1.f, 0.82f, 0.35f);
        g_ren.rect(px, py, iw, 50 * s, got ? gold * 0.12f : C_PANEL2, got ? 0.8f : 0.45f, 4 * s);
        g_ren.rect(px + 10 * s, py + 13 * s, 24 * s, 24 * s, got ? gold : C_DIM * 0.5f, got ? 1.f : 0.5f, 12 * s);
        g_ren.rect(px + 15 * s, py + 18 * s, 14 * s, 14 * s, vec3(0.05f, 0.06f, 0.07f), 1, 7 * s);
        std::string right;
        if (got) { time_t t = (time_t)it->second; char buf[32] = ""; if (t > 0) { struct tm* lt = localtime(&t); if (lt) strftime(buf, sizeof buf, "%d %b %Y", lt); } right = buf; }
        else right = progress(ac.id);
        const float rw = right.empty() ? 0.f : g_ren.textWidth(right, 12 * s) + 12 * s;
        g_ren.text(px + 46 * s, py + 8 * s, 15 * s, ellipsize(ac.title, iw - 56 * s - rw, 15 * s), got ? C_TEXT : C_DIM, 1);
        g_ren.text(px + 46 * s, py + 28 * s, 12 * s, ellipsize(!got && ac.secret ? "A secret: keep flying" : ac.desc, iw - 56 * s - rw, 12 * s), C_DIM, got ? 1.f : 0.7f);
        if (!right.empty()) g_ren.text(px + iw - 10 * s, py + 18 * s, 12 * s, right, got ? gold : C_DIM, 1, 2);
        py += 56 * s;
      }
    } else if (logPage == 2) {   // the passport: every airport landed at, every landmark found, the regions, the sightings
      const bool region = g_world.procedural && g_world.regionRecipe;
      const std::string here = region ? g_world.regionRecipe->descriptor.name : std::string("the Solace Islands");
      if (region) { g_ren.text(px, py, 12 * s, "YOU ARE IN THE " + upperS(here), C_ACCENT, 1, 0, false); py += 26 * s; }
      header(px, py, iw, fmt("AIRPORTS  %d / %d", region ? profile.regionAirports(g_world.regionId) : profile.islandAirports(),
                             region && g_world.regionRecipe ? (int)g_world.regionRecipe->airports.size() : (int)g_world.airports.size())); py += 30 * s;
      for (const Airport& a : g_world.airports) {
        const bool got = profile.airports.count(airportStamp(a)) > 0;
        g_ren.rect(px, py + 3 * s, 10 * s, 10 * s, got ? vec3(1.f, 0.82f, 0.35f) : C_DIM * 0.4f, got ? 1.f : 0.5f, 5 * s);
        g_ren.text(px + 20 * s, py, 14 * s, a.code, got ? vec3(1.f, 0.85f, 0.45f) : C_DIM, 1);
        g_ren.text(px + 74 * s, py, 14 * s, ellipsize(a.name, iw - 170 * s, 14 * s), got ? C_TEXT : C_DIM, got ? 1.f : 0.7f);
        g_ren.text(px + iw, py, 12 * s, got ? "STAMPED" : "", vec3(1.f, 0.82f, 0.35f), 1, 2, false);
        py += 22 * s;
      }
      py += 14 * s;
      const auto& sites = profileSites();
      header(px, py, iw, fmt("LANDMARKS  %d / %d", region ? profile.regionSites(g_world.regionId) : profile.islandSites(), (int)sites.size())); py += 30 * s;
      for (const Site& st : sites) {
        const bool got = profile.sites.count(st.id) > 0;
        g_ren.rect(px, py + 3 * s, 10 * s, 10 * s, got ? C_GOOD : C_DIM * 0.4f, got ? 1.f : 0.5f, 2 * s);
        g_ren.text(px + 20 * s, py, 14 * s, ellipsize(st.name, iw - 120 * s, 14 * s), got ? C_TEXT : C_DIM, got ? 1.f : 0.7f);
        g_ren.text(px + iw, py, 12 * s, got ? "FOUND" : "fly over it", got ? C_GOOD : C_DIM * 0.8f, 1, 2, false);
        py += 22 * s;
      }
      py += 14 * s;
      // the regions beyond the islands: each one flown over, landed in or with a landmark found
      const auto explored = profile.regionsExplored();
      header(px, py, iw, fmt("REGIONS BEYOND THE ISLANDS  %d", (int)explored.size())); py += 30 * s;
      if (explored.empty()) {
        for (auto& line : wrap("Fly out past the islands in Free Flight. The regions beyond them have their own airports to land at and landmarks to find.", iw - 20 * s, 12 * s)) {
          g_ren.text(px + 20 * s, py, 12 * s, line, C_DIM, 0.9f); py += 17 * s;
        }
      }
      for (const std::string& id : explored) {
        const auto d = procedural::find(id);
        if (!d) continue;
        const bool current = region && id == g_world.regionId;
        const int got = profile.regionAirports(id), all = regionAirportCount(id), found = profile.regionSites(id);
        g_ren.rect(px, py + 3 * s, 10 * s, 10 * s, all > 0 && got >= all ? vec3(1.f, 0.82f, 0.35f) : current ? C_ACCENT : C_DIM * 0.6f, 1.f, 5 * s);
        g_ren.text(px + 20 * s, py, 14 * s, ellipsize(d->name, iw - 190 * s, 14 * s), C_TEXT, 1);
        g_ren.text(px + iw, py, 12 * s, fmt("%d / %d airports  %d found", got, all, found), got >= all && all > 0 ? vec3(1.f, 0.82f, 0.35f) : C_DIM, 1, 2, false);
        py += 18 * s;
        g_ren.text(px + 20 * s, py, 11 * s, fmt("%s %s%s", procedural::scaleName(d->scale), lowerS(procedural::archetypeName(d->archetype)).c_str(), current ? "  /  you are here" : ""), C_DIM, 0.8f);
        py += 22 * s;
      }
      py += 14 * s;
      header(px, py, iw, "SIGHTINGS"); py += 30 * s;
      const std::pair<const char*, const char*> seen[] = {{"ufo", "The visitors"}, {"display", "The Nocturne display pair"}};
      for (auto& sg : seen) {
        const bool got = profile.seen.count(sg.first) > 0;
        g_ren.text(px + 20 * s, py, 14 * s, got ? sg.second : "???", got ? C_TEXT : C_DIM, got ? 1.f : 0.6f);
        py += 22 * s;
      }
    } else {
      float fraction = g_story.empty() ? 0 : (float)career.storyIndex / g_story.size();
      g_ren.text(px, py, 12 * s, fmt("%02d / %02d MISSIONS COMPLETE", career.storyIndex, (int)g_story.size()), C_ACCENT, 1, 0, false); py += 26 * s;
      g_ren.rect(px, py, iw, 4 * s, C_DIM, 0.2f, 2 * s);
      g_ren.rect(px, py, std::max(2 * s, iw * fraction), 4 * s, C_ACCENT, 1, 2 * s); py += 30 * s;
      const char* chapters[] = {"Flight school", "Private pilot", "Commercial pilot", "Owner-operator", "Airline captain"};
      int last = -1;
      for (int i = 0; i < (int)g_story.size(); ++i) {
        const Contract& c = g_story[i];
        if (c.chapter != last) { last = c.chapter; py += 12 * s; header(px, py, iw, fmt("CHAPTER %02d / %s", c.chapter + 1, chapters[c.chapter])); py += 30 * s; }
        bool done = i < career.storyIndex, current = i == career.storyIndex;
        if (current) card(px, py - 5 * s, iw, 34 * s, true, false, C_ACCENT);
        g_ren.text(px + 14 * s, py + 3 * s, 11 * s, fmt("%02d", i + 1), done ? C_GOOD : C_DIM, 1, 0, false);
        g_ren.text(px + 48 * s, py, 15 * s, ellipsize(c.title, iw - 146 * s, 15 * s), current ? C_TEXT : done ? C_DIM : C_DIM * 0.65f, 1);
        g_ren.text(px + iw - 12 * s, py + 3 * s, 10 * s, current ? "UP NEXT" : done ? "COMPLETE" : "LOCKED", current ? C_ACCENT : done ? C_GOOD : C_DIM * 0.6f, 1, 2, false);
        py += 38 * s;
      }
    }
    total[column] = py + scroll[column] - top;
    g_ren.uiClipOff();
    float track = bottom - top, limit = std::max(0.f, total[column] - track);
    if (limit > 0) {
      clipFade(bx + 12 * s, bottom, bw - 24 * s, 20 * s, panelAt(y, h, bottom));
      float thumb = std::max(24 * s, track * track / total[column]);
      g_ren.rect(bx + bw - 7 * s, top, 2 * s, track, C_DIM, 0.15f);
      g_ren.rect(bx + bw - 7 * s, top + (track - thumb) * std::min(scroll[column] / limit, 1.f), 2 * s, thumb, C_ACCENT, 0.8f);
    }
    focusScrollPixels(columnFocus0, uid(0, (float)column, "logbook-scroll"), bx, top, bw, bottom, scroll[column], wheeled, limit > 0);
  }
}

void Game::drawSettings(float x, float y, float w, float h) {
  float s = S();
  float py = y;
  g_ren.text(x, py, 24 * s, "Settings", C_TEXT, 1);
  {  // page tabs: general settings / controls, with a sliding underline
    const char* pages[] = {"GENERAL", "CONTROLS", "JOYSTICK"};
    float tx = x + 150 * s, tw = std::min(140 * s, (w - 150 * s - 16 * s) / 3.f);
    for (int i = 0; i < 3; i++) {
      bool take = false;
      const bool foc = focusHere(0x5E7700u + (uint32_t)i, tx + i * (tw + 8 * s), py - 2 * s, tw, 32 * s, &take);
      bool hov = hovered(tx + i * (tw + 8 * s), py - 2 * s, tw, 32 * s) || foc;
      float hk = anim(uid(tx + i * tw, py, "stab"), hov ? 1.f : 0.f, 14);
      if (hk > 0.01f) g_ren.rectGrad(tx + i * (tw + 8 * s), py - 2 * s, tw, 32 * s, vec3(0.05f, 0.16f, 0.24f), vec3(0.02f, 0.06f, 0.1f), 0.8f * hk, 3 * s);
      g_ren.text(tx + i * (tw + 8 * s) + tw * 0.5f, py + 6 * s, 15 * s, pages[i], settingsPage == i ? C_TEXT : mixc(C_DIM, C_TEXT, hk), 1, 1, false);
      if (((hov && in.mPressed[0]) || take) && settingsPage != i) { settingsPage = i; bindCapture = -1; axisCapture = -1; g_audio.trigger(SFX_CLICK); }
    }
    float ux = anim(0x5e77u, tx + settingsPage * (tw + 8 * s), 14);
    g_ren.glow(ux + 10 * s, py + 28 * s, tw - 20 * s, 2.5f * s, C_ACCENT, 0.5f, 1.2f * s, 7 * s);
    g_ren.rect(ux + 10 * s, py + 28 * s, tw - 20 * s, 2.5f * s, C_ACCENT, 1);
  }
  py += 44 * s;
  if (settingsPage == 1) { drawControls(x, py, w, y + h - py); return; }
  if (settingsPage == 2) { drawJoystick(x, py, w, y + h - py); return; }
  header(x, py, std::min(w, 620 * S()), "DISPLAY / AUDIO / CONTROLS"); py += 28 * s;
  // the controls scroll in a clipped region of their own (mouse wheel, the right stick; keyboard or D-pad focus scrolls
  // its control into view): every one stays reachable at any resolution and UI scale (the review of v3.31.0, U3: five
  // rows sat below a 1080p screen). A control outside the region takes no clicks.
  const float rs = 42 * s, regTop = py, regBot = y + h - 24 * s;
  static float genScroll = 0.f, genMax = 0.f;
  bool genWheeled = false;
  if (hovered(x - 8 * s, regTop, w + 16 * s, regBot - regTop) && in.wheel != 0) { genScroll -= in.wheel * 48.f * s; in.wheel = 0; genWheeled = true; }
  genScroll = std::clamp(genScroll, 0.f, genMax);
  buttonContentOffsetY = genScroll;
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
    {   // the track in the D-pad's walk: with the focus on it, left / right step the value
      int st = 0; focusAdjust(uid(x, py + buttonContentOffsetY, "slider:" + label), x + 296 * s, py, 200 * s, 32 * s, &st);
      if (st) v = clampf(v + st * step, lo, hi);
    }
    if (button(x + 506 * s, py, 36 * s, 32 * s, "+")) v = clampf(v + step, lo, hi);
    g_ren.text(x + 556 * s, py + 6 * s, 16 * s, disp, C_TEXT, 1);
    py += rs;
  };
  auto toggle = [&](const std::string& label, bool& v, const char* on, const char* off) {
    g_ren.text(x, py + 6 * s, 16 * s, label, C_DIM, 1);
    if (button(x + 250 * s, py, 200 * s, 32 * s, std::string(v ? on : off) + "##" + label, true, v)) v = !v;
    py += rs;
  };
  // a setting's explanation under its buttons, wrapped to the width beside the labels (at 140% UI scale on a 720p
  // screen one line ran off the panel: the review of v3.33.0, U2)
  auto note = [&](const std::string& t) {
    for (auto& l : wrap(t, std::max(w - 250 * s, 120 * s), 12.5f * s)) { g_ren.text(x + 250 * s, py, 12.5f * s, l, C_DIM, 0.85f, 0, false); py += 17 * s; }
    py += 7 * s;
  };
  g_ren.text(x, py + 6 * s, 16 * s, "Render resolution", C_DIM, 1);
  {
    const char* rm[] = {"Native", "Auto", "85%", "75%", "67%"};
    for (int i = 0; i < 5; i++) if (button(x + 250 * s + i * 78 * s, py, 72 * s, 32 * s, rm[i], true, set.resMode == i)) set.resMode = i;
    py += 36 * s;
    note(fmt("rendered at %dx%d (%.0f%%), upscaled to %dx%d by the temporal AA", (int)(g_ren.W * g_ren.renderScale), (int)(g_ren.H * g_ren.renderScale),
             g_ren.renderScale * 100.f, g_ren.W, g_ren.H));
  }
  g_ren.text(x, py + 6 * s, 16 * s, "Frame rate", C_DIM, 1);
  {
    static const int fr[] = {0, 30, 60, 90, 120, 144, 240};
    for (int i = 0; i < 7; i++) {
      std::string l = fr[i] ? fmt("%d", fr[i]) : std::string("Display");
      if (button(x + 250 * s + i * 62 * s, py, 58 * s, 32 * s, l, true, set.fpsTarget == fr[i])) { set.fpsTarget = fr[i]; wantPacing = true; }
    }
    py += 36 * s;
    note(fmt("%s: %.0f fps now, GPU %s%s", set.fpsTarget ? fmt("capped at %d", set.fpsTarget).c_str() : fmt("the display's %d Hz, on vsync", monitorHz).c_str(),
             1.f / std::max(fpsAvg, 1e-4f), g_ren.gpuMs > 0 ? fmt("%.1f ms", g_ren.gpuMs).c_str() : "n/a", set.resMode == 1 ? " (Auto resolution holds this rate)" : ""));
  }
  g_ren.text(x, py + 6 * s, 16 * s, "Render quality", C_DIM, 1);
  const char* q[] = {"Low", "Medium", "High"};
  for (int i = 0; i < 3; i++) if (button(x + 250 * s + i * 100 * s, py, 92 * s, 32 * s, q[i], true, set.quality == i)) { set.quality = i; g_ren.quality = i; }
  py += rs;
  if (g_ren.vrsAvailable) {   // (NVIDIA's variable-rate shading: RTX cards)
    toggle("Foveated shading", set.foveate, "Edges first", "Off");
    py -= 6 * s;
    note(set.resMode == 1 ? fmt("Auto softens the edges, then the resolution, only where that buys frames: now %s",
                                g_ren.foveation > 0.f || g_ren.renderScale < 1.f ? fmt("edges %.0f%%, resolution %.0f%%", g_ren.foveation * 100.f, g_ren.renderScale * 100.f).c_str() : "the full picture")
                          : std::string("works with Auto resolution"));
  }
  slider("Master volume", set.master, 0, 1, 0.05f, fmt("%.0f%%", set.master * 100));
  slider("Engine volume", set.engineVol, 0, 1.5f, 0.05f, fmt("%.0f%%", set.engineVol * 100));
  slider("Effects volume", set.sfxVol, 0, 1.5f, 0.05f, fmt("%.0f%%", set.sfxVol * 100));
  float rv = set.radioVol;
  slider("Radio volume", set.radioVol, 0, 1, 0.05f, fmt("%.0f%%", set.radioVol * 100));
  if (rv != set.radioVol) radio.setVolume(radioLevel());
  slider("Voice volume", set.atcVol, 0, 1, 0.05f, fmt("%.0f%%", set.atcVol * 100));
  slider("Music volume", set.musicVol, 0, 1, 0.05f, set.musicVol <= 0.f ? std::string("Off") : fmt("%.0f%%", set.musicVol * 100));
  toggle("Music in the cruise", set.musicFlight, "Quiet ambience", "Off");
  slider("Mouse sensitivity", set.mouseSens, 0.2f, 3.f, 0.1f, fmt("%.1f", set.mouseSens));
  slider("Field of view", set.fov, 45.f, 75.f, 1.f, fmt("%.0f / %.0f deg", set.fov, set.fov + 19.f));   // (the long form clipped even at 1080p: the review of v3.44.0, UI-6)
  py -= 6 * s; note("outside / in the cockpit");
  slider("UI scale", set.uiScale, 0.8f, 1.4f, 0.05f, fmt("%.0f%%", set.uiScale * 100));
  toggle("Menu animation", set.menuMotion, "Ambient motion", "Reduced motion");
  toggle("Head-look", set.headLook, "Leans into turns", "Fixed ahead");
  toggle("Display focus zoom", set.cockpitFocusZoom, "Smooth automatic zoom", "Manual zoom only");
  { bool cb = set.cbHud; toggle("HUD palette", set.cbHud, "Blue / orange (colour-blind)", "Green / red"); if (cb != set.cbHud) applyPalette(set.cbHud); }
  g_ren.text(x + 250 * s, py - 2 * s, 12.5f * s, "The HUD key remembers on / off for each camera view.", C_DIM, 0.85f, 0, false); py += 20 * s;
  py += 6 * s; header(x, py, std::min(w, 620 * S()), "FLYING"); py += 28 * s;
  g_ren.text(x, py + 6 * s, 16 * s, "Flight assists", C_DIM, 1);
  {
    const char* am[] = {"Off", "Keys and pad", "All inputs"};
    for (int i = 0; i < 3; i++) if (button(x + 250 * s + i * 128 * s, py, 122 * s, 32 * s, am[i], true, set.assist == i)) set.assist = i;
    py += 36 * s;
    note(set.assist == 0 ? "Every input moves the controls directly, as in the real aircraft" :
         "The inputs roll and pitch the aircraft; let go and it holds its bank and its climb, the rudder coordinating. Near the stall the nose comes down. Time warp always flies assisted");
  }
  toggle("Chase view", set.camRecentre, "Recentres after 4 s", "Stays where swung");
  slider("Camera shake", set.camShake, 0.f, 1.5f, 0.1f, set.camShake <= 0.f ? std::string("Off") : fmt("%.0f%%", set.camShake * 100));
  py -= 6 * s; note("touchdowns, turbulence, the stall buffet, high g");
  toggle("Gamepad rumble", set.rumble, "On", "Off");
  toggle("Speed sensation", set.speedFov, "Chase view widens with speed", "Fixed view");
  toggle("Random failures", set.failures, "On (worn and rented aircraft)", "Off");
  g_ren.text(x, py + 6 * s, 16 * s, "Air", C_DIM, 1);
  if (button(x + 250 * s, py, 160 * s, 32 * s, "Calm", true, set.air == 0)) set.air = 0;
  if (button(x + 418 * s, py, 160 * s, 32 * s, "As forecast", true, set.air == 1)) set.air = 1;
  py += 36 * s; note("Calm takes two thirds off every gust and the turbulence");
  toggle("Flight HUD", set.hudEssential, "Essentials", "Full");
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
  g_ren.text(x, py, 14 * s, "Somnium Volandi " + version, C_DIM, 0.8f);
  py += 22 * s;
  genMax = std::max(0.f, py + genScroll - regBot);
  buttonContentOffsetY = 0;
  hitClipOn = false;
  g_ren.uiClipOff();
  if (genMax > 0.f) {   // where the list stands: a thin track at the right edge
    clipFade(x - 8 * s, regBot, w + 16 * s, 20 * s, panelAt(y, h, regBot));
    const float th = regBot - regTop, bh = std::max(24 * s, th * th / (th + genMax)), by = regTop + (th - bh) * (genScroll / genMax);
    g_ren.rect(x + w + 2 * s, regTop, 3 * s, th, C_ACCENT, 0.12f, 1.5f * s);
    g_ren.rect(x + w + 2 * s, by, 3 * s, bh, C_ACCENT, 0.7f, 1.5f * s);
  }
  // the control with keyboard / D-pad focus scrolled into view (next frame draws it there); the list scrolled by the
  // right stick takes the focus along
  focusScrollPixels(focus0, uid(0, (float)settingsPage, "settings-scroll"), x - 8 * s, regTop, w + 16 * s, regBot, genScroll, genWheeled, genMax > 0.f);
  saveSettings();
  (void)w;
}

// A joystick button binding's name: its number (a hat's direction), and its device when that isn't the only one there
std::string Game::joyButtonLabel(const JoyButtonBind& b) const {
  if (!b.bound()) return "--";
  static const char* hat[] = {"UP", "RIGHT", "DOWN", "LEFT"};
  std::string l = b.button >= 120 ? fmt("HAT%d %s", (b.button - 120) / 4 + 1, hat[(b.button - 120) % 4]) : fmt("B%d", b.button + 1);
  const int d = joyFind(in.joys, b.dev, b.devName);
  if (d < 0) return l + " (absent)";
  return in.joys.size() > 1 ? l + "  " + b.devName : l;
}

// Controls page: every rebindable action with its keyboard key, gamepad button and joystick button. Click a cell and
// press the new key / button (Esc or Menu cancels, right-click clears); one already used in the same group swaps over.
void Game::drawControls(float x, float y, float w, float h) {
  float s = S(), rowH = 32 * s;
  float nameW = std::min(290 * s, w * 0.34f), cellW = std::min(170 * s, (w - nameW - 52 * s) / 3.f);
  float kx = x + nameW, px = kx + cellW + 12 * s, jx = px + cellW + 12 * s;
  // column titles with device glyphs
  g_ren.text(x, y, 12 * s, "ACTION", C_DIM, 1, 0, false);
  g_ren.rect(kx + 2 * s, y + 1 * s, 16 * s, 10 * s, C_ACCENT, 0.0f); g_ren.rectOutline(kx, y, 18 * s, 12 * s, C_ACCENT, 0.7f, 2 * s, 1 * s);
  for (int i = 0; i < 4; i++) g_ren.rect(kx + 3 * s + i * 3.5f * s, y + 3 * s, 2 * s, 2 * s, C_ACCENT, 0.8f);
  g_ren.rect(kx + 5 * s, y + 7.5f * s, 8 * s, 1.5f * s, C_ACCENT, 0.8f);
  g_ren.text(kx + 26 * s, y, 12 * s, "KEYBOARD", C_ACCENT, 1, 0, false);
  g_ren.rect(px, y + 1 * s, 20 * s, 11 * s, C_ACCENT, 0.0f); g_ren.rectOutline(px, y + 1 * s, 20 * s, 11 * s, C_ACCENT, 0.7f, 5 * s, 1 * s);
  g_ren.rect(px + 4 * s, y + 5.5f * s, 5 * s, 1.5f * s, C_ACCENT, 0.8f); g_ren.rect(px + 5.75f * s, y + 3.75f * s, 1.5f * s, 5 * s, C_ACCENT, 0.8f);
  g_ren.rect(px + 13 * s, y + 4 * s, 2.5f * s, 2.5f * s, C_ACCENT, 0.8f, 1.25f * s); g_ren.rect(px + 15.5f * s, y + 6.5f * s, 2.5f * s, 2.5f * s, C_ACCENT, 0.8f, 1.25f * s);
  g_ren.text(px + 28 * s, y, 12 * s, in.pad ? "CONTROLLER  (ON)" : "CONTROLLER", in.pad ? C_GOOD : C_ACCENT, 1, 0, false);
  g_ren.rect(jx + 7 * s, y + 3 * s, 4 * s, 9 * s, C_ACCENT, 0.8f, 1 * s); g_ren.rect(jx + 3 * s, y + 9 * s, 12 * s, 3 * s, C_ACCENT, 0.8f, 1 * s);   // (a stick on its base)
  g_ren.rect(jx + 6 * s, y, 6 * s, 4 * s, C_ACCENT, 0.8f, 2 * s);
  g_ren.text(jx + 22 * s, y, 12 * s, in.joys.empty() ? "JOYSTICK" : fmt("JOYSTICK  (%d ON)", (int)in.joys.size()), in.joys.empty() ? C_ACCENT : C_GOOD, 1, 0, false);
  y += 22 * s;
  // flattened list: group headers + action rows, scrolled a row at a time
  struct Row { int group, act; };
  std::vector<Row> rows;
  for (int g = 0; g < 4; g++) { rows.push_back({g, -1}); for (int a = 0; a < ACT_COUNT; a++) if (kActions[a].group == g) rows.push_back({g, a}); }
  float footH = 92 * s;
  int visible = std::max(4, (int)((h - footH) / rowH));
  int maxScroll = std::max(0, (int)rows.size() - visible);
  const int ctlBefore = ctlScroll;
  if (hovered(x, y, w, visible * rowH) && in.wheel != 0) { ctlScroll -= (int)in.wheel * 2; in.wheel = 0; }
  if (in.pad && bindCapture < 0 && fabsf(in.ry) > 0.5f) { ctlScrollAcc += in.ry * uiDt * 12.f; }
  while (ctlScrollAcc > 1.f) { ctlScroll--; ctlScrollAcc -= 1.f; }
  while (ctlScrollAcc < -1.f) { ctlScroll++; ctlScrollAcc += 1.f; }
  const bool ctlWheeled = ctlScroll != ctlBefore;
  // (each cell is in the keyboard / D-pad walk, the rows scrolled out of view too: the walk moves into them and the list
  // follows the focus)
  auto cellId = [](int act, int dev) { return 0xB1D00000u + (uint32_t)act * 3u + (uint32_t)dev; };
  ctlScroll = std::clamp(ctlScroll, 0, maxScroll);
  if (focusNav && focusId >= cellId(0, 0) && focusId < cellId(ACT_COUNT, 0)) {
    const int act = (int)((focusId - cellId(0, 0)) / 3u), dev = (int)((focusId - cellId(0, 0)) % 3u);
    for (int i = 0; i < (int)rows.size(); i++) if (rows[i].act == act && (i < ctlScroll || i >= ctlScroll + visible)) {
      if (!ctlWheeled) { if (i < ctlScroll) ctlScroll = i; else ctlScroll = i - visible + 1; break; }
      // (the list scrolled away from the focus by the stick: it goes along, to the nearest action still shown)
      const int from = i < ctlScroll ? ctlScroll : std::min((int)rows.size(), ctlScroll + visible) - 1, dir = i < ctlScroll ? 1 : -1;
      for (int r = from; r >= ctlScroll && r < std::min((int)rows.size(), ctlScroll + visible); r += dir) if (rows[r].act >= 0) { focusId = cellId(rows[r].act, dev); break; }
      break;
    }
  }
  ctlScroll = std::clamp(ctlScroll, 0, maxScroll);
  float sy = anim(0xc7151u, (float)ctlScroll, 18);
  float listTop = y;
  for (int i = 0; i < (int)rows.size(); i++) {
    float ry = listTop + (i - sy) * rowH;
    const Row& r = rows[i];
    const bool shown = !(ry < listTop - 0.5f * s || ry > listTop + (visible - 1) * rowH + 0.5f * s);
    if (!shown) {   // (registered where it would be; the list is scrolling to it - Enter takes it already)
      if (r.act >= 0) for (int dev = 0; dev < 3; dev++) {
        const uint32_t id = cellId(r.act, dev);
        focusList.push_back({id, dev == 2 ? jx : dev ? px : kx, ry + 2 * s, cellW, rowH - 8 * s});
        if (focusNav && focusId == id && bindCapture < 0 && (in.pressed[K_ENTER] || in.pressed[' '])) {
          in.pressed[K_ENTER] = in.pressed[' '] = false; bindCapture = r.act; bindCaptureDev = dev; bindCaptureT = 0; g_audio.trigger(SFX_CLICK);
        }
      }
      continue;
    }
    if (r.act < 0) { header(x, ry + 10 * s, w - 30 * s, kActionGroups[r.group]); continue; }
    const ActionInfo& ai = kActions[r.act];
    bool rowHov = hovered(x, ry, w - 30 * s, rowH - 4 * s);
    float rh = anim(uid(x, (float)r.act, "crow"), rowHov ? 1.f : 0.f, 14);
    if (rh > 0.01f) g_ren.rectGrad(x - 6 * s, ry, w - 24 * s, rowH - 4 * s, vec3(0.04f, 0.12f, 0.18f), vec3(0.02f, 0.06f, 0.1f), 0.6f * rh, 3 * s);
    g_ren.text(x + 6 * s, ry + 6 * s, 15 * s, ai.name, mixc(C_DIM, C_TEXT, 0.6f + 0.4f * rh), 1);
    for (int dev = 0; dev < 3; dev++) {
      float cx = dev == 2 ? jx : dev ? px : kx, cy = ry + 2 * s, cw = cellW, ch = rowH - 8 * s;
      bool cap = bindCapture == r.act && bindCaptureDev == dev;
      bool take = false;
      const bool foc = focusHere(cellId(r.act, dev), cx, cy, cw, ch, bindCapture < 0 ? &take : nullptr);
      bool hov = hovered(cx, cy, cw, ch) || foc;
      int k = set.keyBind[r.act]; unsigned b = set.padBind[r.act];
      const JoyButtonBind& jb = set.joyBind[r.act];
      bool clash = false;
      for (int o = 0; o < ACT_COUNT; o++)
        if (o != r.act && kActions[o].group == ai.group &&
            (dev == 2 ? (jb.bound() && set.joyBind[o].button == jb.button && set.joyBind[o].dev == jb.dev) : dev ? (b && set.padBind[o] == b) : (k && set.keyBind[o] == k))) clash = true;
      bool isDef = dev == 2 ? !jb.bound() : dev ? b == ai.pad : k == ai.key;
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
        std::string lbl = dev == 2 ? ellipsize(joyButtonLabel(jb), cw - 24 * s, 13 * s) : dev ? padName(b) : keyName(k);
        bool none = dev == 2 ? !jb.bound() : dev ? !b : !k;
        // keycap / button chip
        float tw = g_ren.textWidth(lbl, 13 * s) + 16 * s;
        float chx = cx + cw * 0.5f - tw * 0.5f;
        if (!none) {
          g_ren.rect(chx, cy + 3 * s, std::min(tw, cw - 8 * s), ch - 6 * s, ac, 0.12f + 0.1f * hk, dev ? (ch - 6 * s) * 0.5f : 2.5f * s);
          g_ren.rect(chx, cy + ch - 4 * s, tw, 1 * s, ac, 0.5f);
        }
        g_ren.text(cx + cw * 0.5f, cy + ch * 0.5f - 7 * s, 13 * s, lbl, none ? C_DIM * 0.6f : clash ? C_WARN : C_TEXT, 1, 1, false);
        if (!isDef) g_ren.rect(cx + cw - 7 * s, cy + 4 * s, 3.5f * s, 3.5f * s, C_ACCENT, 0.9f, 1.75f * s);   // customised marker
        if ((hov && in.mPressed[0]) || take) { bindCapture = r.act; bindCaptureDev = dev; bindCaptureT = 0; g_audio.trigger(SFX_CLICK); }
        if (hov && in.mPressed[1]) { if (dev == 2) set.joyBind[r.act] = JoyButtonBind(); else if (dev) set.padBind[r.act] = 0; else set.keyBind[r.act] = 0; saveSettings(); g_audio.trigger(SFX_CLICK); }
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
  g_ren.text(x, fy + 52 * s, 12.5f * s, bindCapture >= 0 ? "Esc / Menu cancels" : "Click a cell to rebind  //  right-click to clear  //  joystick axes: the Joystick tab", C_ACCENT, 0.9f, 0, false);
  if (button(x + w - 30 * s - 190 * s, fy + 46 * s, 190 * s, 32 * s, "Reset to defaults")) { set.resetBindings(); bindCapture = -1; saveSettings(); }
}

// Settings -> Joystick: the joysticks, throttles and pedals connected, and the five flight axes - each bound by moving
// the control, with its direction, dead zone and curve, and a live meter of what the aircraft gets
void Game::drawJoystick(float x, float y, float w, float h) {
  const float s = S();
  float py = y;
  header(x, py, w - 16 * s, "DEVICES"); py += 28 * s;
  if (in.joys.empty()) {
    g_ren.text(x + 6 * s, py, 14 * s, "No joystick, throttle or pedals connected. Plug one in: it is found at once.", C_DIM, 1); py += 22 * s;
  } else for (size_t d = 0; d < in.joys.size() && d < 4; d++) {
    const JoyState& j = in.joys[d];
    g_ren.text(x + 6 * s, py, 14 * s, ellipsize(j.name, w * 0.55f, 14 * s), C_TEXT, 1);
    g_ren.text(x + w * 0.6f, py, 13 * s, j.hats ? fmt("%d axes, %d buttons, %d hat%s", j.axes, j.buttons, j.hats, j.hats > 1 ? "s" : "") : fmt("%d axes, %d buttons", j.axes, j.buttons), C_DIM, 1);
    py += 20 * s;
  }
  py += 8 * s;
  header(x, py, w - 16 * s, "FLIGHT AXES"); py += 30 * s;
  // the axes in a clipped region of their own, scrolled (the wheel, the right stick, the focus) when the panel is too
  // short for five rows - at a large UI scale on a small screen the last rows ran under the note at the foot
  const float regTop = py, regBot = y + h - 40 * s;
  static float axScroll = 0.f, axMax = 0.f;
  bool axWheeled = false;
  if (hovered(x - 8 * s, regTop, w + 16 * s, regBot - regTop) && in.wheel != 0) { axScroll -= in.wheel * 48.f * s; in.wheel = 0; axWheeled = true; }
  axScroll = std::clamp(axScroll, 0.f, axMax);
  buttonContentOffsetY = axScroll;
  const size_t focus0 = focusList.size();
  g_ren.uiClip(x - 8 * s, regTop, x + w + 8 * s, regBot);
  hitClipOn = true; hitClip[0] = x - 8 * s; hitClip[1] = regTop; hitClip[2] = x + w + 8 * s; hitClip[3] = regBot;
  py -= axScroll;
  const float rowH = std::min(62 * s, std::max(56 * s, (regBot - regTop) / AX_COUNT));
  const float bx = x + std::min(210 * s, w * 0.27f), bw = std::min(270 * s, w * 0.36f), mx = bx + bw + 16 * s, mw = std::max(40 * s, x + w - 16 * s - mx);
  const float u = std::min(1.f, (w - 32 * s) / (600 * s));   // (the second line's controls, narrower in a small window)
  for (int i = 0; i < AX_COUNT; i++) {
    AxisBind& b = set.axisBind[i];
    const bool lever = kFlightAxes[i].unipolar;
    const int d = b.bound() ? joyFind(in.joys, b.dev, b.devName) : -1;
    g_ren.text(x + 6 * s, py + 5 * s, 15 * s, kFlightAxes[i].name, C_TEXT, 1);
    // the binding: click and move the control; right-click clears
    const bool cap = axisCapture == i;
    std::string lbl = !b.bound() ? std::string("Not set - click, then move it") : std::string(joyAxisName(b.axis)) + "  -  " + b.devName + (d < 0 ? "  (absent)" : "");
    if (cap) {
      const float pulse = 0.5f + 0.5f * sinf(realTime * 7.f);
      g_ren.glow(bx, py, bw, 28 * s, C_ACCENT, 0.25f + 0.25f * pulse, 3 * s, 10 * s);
      g_ren.rectGrad(bx, py, bw, 28 * s, C_ACCENT * 0.5f, C_ACCENT * 0.25f, 0.95f, 3 * s);
      static const char* ask[AX_COUNT] = {"PULL THE STICK RIGHT BACK", "STICK FULLY RIGHT", "FULL RIGHT RUDDER", "PUSH THE LEVER TO FULL", "PRESS THE BRAKES FULLY"};
      g_ren.text(bx + bw * 0.5f, py + 7 * s, 12 * s, ask[i], C_TEXT, 0.7f + 0.3f * pulse, 1, false);   // (the way it moves sets its direction)
      g_ren.rect(bx, py + 26 * s, bw * clampf(1.f - bindCaptureT / 8.f, 0, 1), 2 * s, C_TEXT, 0.8f);
    } else if (button(bx, py, bw, 28 * s, ellipsize(lbl, bw - 20 * s, 13 * s) + "##axis-bind-" + std::to_string(i), true, false) && axisCapture < 0) {
      axisCapture = i; bindCaptureT = 0;
      axisCaptureRest.assign(in.joys.size(), {});
      for (size_t k = 0; k < in.joys.size(); k++) for (int a = 0; a < kJoyAxes; a++) axisCaptureRest[k][a] = in.joys[k].axis[a];
    }
    if (hovered(bx, py, bw, 28 * s) && in.mPressed[1]) { b = AxisBind(); saveSettings(); g_audio.trigger(SFX_CLICK); }
    // the live meter: what the aircraft gets (the bar) and where the control is (the tick)
    g_ren.rect(mx, py + 10 * s, mw, 8 * s, C_ACCENT, 0.14f, 4 * s);
    if (d >= 0) {
      const float raw = in.joys[d].axis[b.axis] * (b.invert ? -1.f : 1.f), v = shapeAxis(in.joys[d].axis[b.axis], b, lever);
      if (lever) g_ren.rect(mx, py + 10 * s, mw * v, 8 * s, C_ACCENT, 0.85f, 4 * s);
      else { const float c = mx + mw * 0.5f, e = c + mw * 0.5f * v; g_ren.rect(std::min(c, e), py + 10 * s, fabsf(e - c), 8 * s, C_ACCENT, 0.85f, 2 * s); g_ren.rect(c - 0.5f * s, py + 6 * s, 1 * s, 16 * s, C_DIM, 0.8f); }
      g_ren.rect(mx + mw * (raw + 1.f) * 0.5f - 1 * s, py + 5 * s, 2 * s, 18 * s, C_TEXT, 0.9f);
      g_ren.text(mx + mw, py - 9 * s, 11 * s, lever ? fmt("%.0f%%", v * 100.f) : fmt("%+.0f%%", v * 100.f), C_DIM, 1, 2, false);
    }
    // the second line: direction, dead zone, curve, clear
    float lx = x + 16 * s, ly = py + 32 * s, lh = std::min(24 * s, rowH - 36 * s), sw = 26 * s * u, vw = 128 * s * u;
    const std::string sfx = "##axis-" + std::to_string(i);
    if (button(lx, ly, 120 * s * u, lh, std::string(b.invert ? "Reversed" : "Normal") + sfx + "-inv")) { b.invert = !b.invert; saveSettings(); }
    lx += 120 * s * u + 14 * s * u;
    auto stepper = [&](const char* what, float& v, float step, float lo, float hi) {
      if (button(lx, ly, sw, lh, "-" + sfx + what + "-")) { v = clampf(roundf((v - step) / step) * step, lo, hi); saveSettings(); }
      g_ren.text(lx + sw + vw * 0.5f, ly + lh * 0.5f - 6.5f * s, 12.5f * s, fmt("%s %.0f%%", what, v * 100.f), C_TEXT, 1, 1, false);
      if (button(lx + sw + vw, ly, sw, lh, "+" + sfx + what + "+")) { v = clampf(roundf((v + step) / step) * step, lo, hi); saveSettings(); }
      lx += 2 * sw + vw + 14 * s * u;
    };
    stepper(lever ? "Ends" : "Dead zone", b.dead, 0.01f, 0.f, 0.45f);
    stepper("Curve", b.curve, 0.1f, 0.f, 1.f);
    if (b.bound() && button(lx, ly, 80 * s * u, lh, "Clear" + sfx + "-clear")) { b = AxisBind(); saveSettings(); }
    py += rowH;
  }
  axMax = std::max(0.f, py + axScroll - regBot);
  buttonContentOffsetY = 0; hitClipOn = false; g_ren.uiClipOff();
  if (axMax > 0.f) {   // where the list stands: a thin track at the right edge
    clipFade(x - 8 * s, regBot, w + 16 * s, 20 * s, panelAt(y, h, regBot));
    const float th = regBot - regTop, bh = std::max(24 * s, th * th / (th + axMax)), by = regTop + (th - bh) * (axScroll / axMax);
    g_ren.rect(x + w + 2 * s, regTop, 3 * s, th, C_ACCENT, 0.12f, 1.5f * s);
    g_ren.rect(x + w + 2 * s, by, 3 * s, bh, C_ACCENT, 0.7f, 1.5f * s);
  }
  focusScrollPixels(focus0, uid(0, 2, "settings-scroll"), x - 8 * s, regTop, w + 16 * s, regBot, axScroll, axWheeled, axMax > 0.f);
  g_ren.text(x, y + h - 28 * s, 12 * s, ellipsize("Keys and the gamepad keep working alongside. A throttle lever takes over when it moves; a key or trigger takes it back.", w, 12 * s), C_DIM, 0.85f, 0, false);
}

void Game::drawRadioPanel(float x, float y) {
  float s = S();
  const int rows = std::min((int)stations.size(), std::max(4, (int)((g_ren.H - y - 140 * s) / (34 * s))));
  const bool none = stations.empty();
  const int visible = none ? 3 : std::min(rows, 12);   // (an empty list: the room says how to add stations)
  float w = 440 * s, h = (118 + 34 * visible) * s;
  radioRect[0] = x; radioRect[1] = y; radioRect[2] = w; radioRect[3] = h;   // (next frame's widgets beneath it keep off)
  radioDrawing = true;
  struct Done { bool& f; ~Done() { f = false; } } done{radioDrawing};
  panel(x, y, w, h, 0.92f);
  g_ren.text(x + 18 * s, y + 14 * s, 20 * s, "Internet Radio", C_TEXT, 1);
  if (button(x + 196 * s, y + 12 * s, 90 * s, 26 * s, "Reload")) { loadStations(); radioScroll = 0; }   // (the list as the file now has it)
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
    // (its identity is its place in the list, not the station: the focus keeps its row as the right stick scrolls it)
    if (button(x + 14 * s, py, bw, 30 * s, ellipsize(stations[i].first, bw - 20 * s, std::min(30 * s * 0.46f, 18 * s)) + "##radio-row-" + std::to_string(k), true, cur)) {
      set.radioStation = i; radio.setVolume(radioLevel()); radio.play(stations[i].second); saveSettings();
    }
    py += 34 * s;
  }
  if (none) {   // no stations: none ship with the game, so the player adds their own
    const std::string file = saveDir.empty() ? std::string("radio_stations.txt in the game folder")
                           : saveDir + (saveDir.find('\\') != std::string::npos ? "\\" : "/") + "radio_stations.txt";   // (Windows' own separator for Explorer)
    g_ren.text(x + 18 * s, listY + 2 * s, 15 * s, "No stations yet.", C_TEXT, 1);
    g_ren.text(x + 18 * s, listY + 24 * s, 13 * s, "Add one per line, as  Name|URL , to", C_DIM, 1);
    g_ren.text(x + 18 * s, listY + 44 * s, 12 * s, ellipsize(file, w - 36 * s, 12 * s), C_ACCENT, 1, 0, false);
    if (platformOpen && !saveDir.empty() && button(x + 14 * s, listY + 68 * s, 160 * s, 30 * s, "Edit station list")) { loadStations(); platformOpen(file); }
    py = listY + listH;
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
  if (button(x + 150 * s, py + 4 * s, 40 * s, 30 * s, "-")) { set.radioVol = clampf(set.radioVol - 0.1f, 0, 1); radio.setVolume(radioLevel()); }
  g_ren.text(x + 200 * s, py + 10 * s, 15 * s, fmt("Vol %.0f%%", set.radioVol * 100), C_TEXT, 1);
  if (button(x + 290 * s, py + 4 * s, 40 * s, 30 * s, "+")) { set.radioVol = clampf(set.radioVol + 0.1f, 0, 1); radio.setVolume(radioLevel()); }
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
  // (the image repeats with the map: across its seam, the far side's islands)
  float u0 = (plane.pos.x - range + WRAP_HALF) / WRAP_SPAN, v0 = (plane.pos.z - range + WRAP_HALF) / WRAP_SPAN;
  float u1 = (plane.pos.x + range + WRAP_HALF) / WRAP_SPAN, v1 = (plane.pos.z + range + WRAP_HALF) / WRAP_SPAN;
  g_ren.image(g_ren.minimapTex, x, y, sz, sz, u0, v0, u1, v1, 0.95f);
  g_ren.flushUIPublic();
  auto toS = [&](float wx_, float wz, bool& inside) {   // (at the copy nearest the aircraft: across the map's seam, the far side's)
    float px = x + (g_world.wrap(wx_ - plane.pos.x) + range) / (2 * range) * sz, py = y + (g_world.wrap(wz - plane.pos.z) + range) / (2 * range) * sz;
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
    const vec3 col = &a == &dest() ? C_ACCENT : a.water() ? vec3(0.45f, 0.8f, 1.f) : vec3(1, 1, 1);
    for (int d = 0; d < (a.water() ? 3 : 1); d++) {   // (a seaplane base's water lane dashed)
      const float t0 = a.water() ? -0.5f + d * 0.4f : -0.5f, t1 = a.water() ? t0 + 0.2f : 0.5f;
      g_ren.line(p.x + dir.x * a.length * t0 * k, p.y + dir.z * a.length * t0 * k, p.x + dir.x * a.length * t1 * k, p.y + dir.z * a.length * t1 * k, 3 * s, col, 1);
    }
    g_ren.text(p.x + 6 * s, p.y + 4 * s, 11 * s, a.code, col, 1);
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
    g_ren.text(W * 0.5f, H - 46 * s, 17 * s, padPrompts() ? "PRESS  A  TO SKIP" : "PRESS  ENTER  TO SKIP", C_TEXT, a, 1);
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
  std::string combatTarget;
  if(militaryFlight) {
    const auto& m=hiveCombat.mission;
    if(m.status==hive::MissionStatus::Extract) { target=m.config.extraction; combatTarget="EXTRACTION"; }
    else if(m.config.kind==hive::MissionKind::Recon || m.config.kind==hive::MissionKind::Defense) {
      float nearest=1e20f;
      for(int i=0;i<m.config.objectiveCount;++i) { const auto& o=m.config.objectives[i]; if(o.scanned) continue;
        float dd=length(hiveCombat.displacement(plane.pos,o.position)); if(dd<nearest) { nearest=dd;target=o.position;combatTarget=(m.config.kind==hive::MissionKind::Recon?"SCAN SITE ":"DEFEND ")+militarySiteLabels[i]; }
      }
    } else { for(int i=0;i<m.config.targetCount;++i) { auto* a=hiveCombat.find(m.config.targetIds[i]); if(a && a->alive) { target=a->position;combatTarget="STRIKE TARGET";break; } } }
  }
  target.x = g_world.nearPoint(target.x, plane.pos.x); target.z = g_world.nearPoint(target.z, plane.pos.z);   // (the short way round the map)
  vec3 to = target - plane.pos;
  float dist = length(vec3(to.x, 0, to.z));
  float brg = wrapDeg360(atan2f(to.x, -to.z) / DEG);
  float gs = length(vec3(plane.vel.x, 0, plane.vel.z));
  vec3 mag(1.f, 0.35f, 1.f);
  const vec3 APC(0.3f, 0.95f, 1.f);
  float hdg = plane.heading();
  // the bottom rows' layout (the systems tiles over the bottom strip), which the target marker keeps clear of too
  const float stripH = cockpit ? 0.f : 46 * s, pfd = 148 * s;
  const float tileW = 80 * s, tileH = 38 * s, tileGap = 5 * s; const int tileCols = 4;
  int tileRows = spc.special >= 2 ? 3 : 2;
  const float tilesTop = H - stripH - 10 * s - tileRows * tileH - (tileRows - 1) * tileGap;
  // ================================================================ the target in the world (first: under the rest of
  // the HUD, and behind the aircraft - its diamond a point in the world, hidden where the airframe or the cockpit is in
  // front of it; the pointer at the screen's edge is the HUD's own, over the cockpit but under every panel)
  if (!crashed) {
    auto label = [&](float x, float y, const std::string& t) {
      float tw2 = g_ren.textWidth(t, 13 * s);
      g_ren.rect(x - tw2 * 0.5f - 6 * s, y - 2 * s, tw2 + 12 * s, 19 * s, vec3(0.01f, 0.02f, 0.04f), 0.7f, 4 * s);
      g_ren.text(x, y, 13 * s, t, mag, 1, 1);
    };
    vec3 tgt3 = target; if (!toWp && combatTarget.empty()) tgt3.y = d.elev + 3.f;
    float sx, sy;
    vec3 rel3 = tgt3 - fp.camPos;
    float zc = dot(rel3, -fp.camBack);
    const float bandT = band + 100 * s, bandB = cockpit ? H - 70 * s : tilesTop - 40 * s, bandC = 0.5f * (bandT + bandB), bandH = std::max(40 * s, 0.5f * (bandB - bandT));
    bool onS = zc > 1.f && g_ren.project(fp, tgt3, sx, sy) && sx > rail + 20 * s && sx < W - rail - 20 * s && sy > bandT && sy < bandB;
    std::string lab = dist < 1000.f ? fmt("%.0f m", length(rel3)) : fmt("%.1f km", dist / 1000.f);
    if(!combatTarget.empty()) lab=combatTarget+" / "+lab;
    if ((toWp || !combatTarget.empty()) && fabsf(target.y - plane.pos.y) > 45.f) lab += fmt("  %s%s", target.y > plane.pos.y ? "+" : "-", fmtAlt(fabsf(target.y - plane.pos.y)).c_str());
    if (onS) {
      g_ren.uiBehindAircraft(true);
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
      g_ren.uiBehindAircraft(false);
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
  // the best run's ghost on a gate run: where it was at this moment of its run (from the first gate)
  if (!crashed && contract.type == CT_TRIAL && trialT0 >= 0.f && trialT1 < 0.f && ghostBest.size() > 1) {
    const float gt = (flightClock - trialT0) / kGhostDt;
    const int i0 = std::min((int)gt, (int)ghostBest.size() - 1), i1 = std::min(i0 + 1, (int)ghostBest.size() - 1);
    const vec3 gp = ghostBest[i0] + (ghostBest[i1] - ghostBest[i0]) * clampf(gt - i0, 0.f, 1.f);
    const vec3 rel = gp - fp.camPos;
    float gx, gy;
    if (gt < (float)ghostBest.size() && dot(rel, -fp.camBack) > 1.f && length(rel) < 6000.f && g_ren.project(fp, gp, gx, gy)) {
      const vec3 gc(0.45f, 0.85f, 1.f);
      const float pulse = 0.75f + 0.25f * sinf(realTime * 6.f), r = clampf(1400.f / std::max(length(rel), 1.f), 8.f, 26.f) * s;
      g_ren.uiBehindAircraft(true);
      g_ren.glow(gx - r, gy - r, r * 2, r * 2, gc, 0.65f * pulse, r, 12 * s);
      g_ren.rect(gx - 2.5f * s, gy - 2.5f * s, 5 * s, 5 * s, gc, 1, 2.5f * s);
      hudRing(gx, gy, r, 1.6f * s, gc, 0.9f, 32);
      const float ahead = dot(gp - plane.pos, normalize(plane.vel + vec3(0, 0, 1e-3f)));
      const std::string t = fmt("BEST  %s%.0f m", ahead >= 0 ? "+" : "-", fabsf(ahead));
      const float tw = g_ren.textWidth(t, 12 * s), lx = clampf(gx, rail + tw * 0.5f + 10 * s, W - rail - tw * 0.5f - 10 * s);
      g_ren.rect(lx - tw * 0.5f - 5 * s, gy + r + 3 * s, tw + 10 * s, 17 * s, vec3(0.01f, 0.02f, 0.04f), 0.65f, 4 * s);
      g_ren.text(lx, gy + r + 4 * s, 12 * s, t, gc, 1, 1);
      g_ren.uiBehindAircraft(false);
    }
  }
  // the passengers' sightseeing request: the landmark they asked for, in gold (an arrow at the edge when out of view)
  if (!crashed && sightReq >= 0 && sightAsked && !sightDone && sightReq < (int)profileSites().size()) {
    const Site& st = profileSites()[sightReq];
    const vec3 sp(st.pos.x, std::max(st.pos.y, 0.f) + 80.f, st.pos.z), rel = sp - fp.camPos;
    const vec3 gold(1.f, 0.82f, 0.35f);
    const std::string t = fmt("%s  %.1f km", st.name.c_str(), length(vec3(rel.x, 0, rel.z)) / 1000.f);
    float gx, gy;
    if (dot(rel, -fp.camBack) > 1.f && g_ren.project(fp, sp, gx, gy) && gx > rail + 20 * s && gx < W - rail - 20 * s && gy > band + 60 * s && gy < H - 120 * s) {
      const float r = 9 * s;
      g_ren.uiBehindAircraft(true);
      g_ren.line(gx - r, gy, gx, gy - r, 2 * s, gold, 0.9f); g_ren.line(gx, gy - r, gx + r, gy, 2 * s, gold, 0.9f);
      g_ren.line(gx + r, gy, gx, gy + r, 2 * s, gold, 0.9f); g_ren.line(gx, gy + r, gx - r, gy, 2 * s, gold, 0.9f);
      const float tw = g_ren.textWidth(t, 12 * s), lx = clampf(gx, rail + tw * 0.5f + 10 * s, W - rail - tw * 0.5f - 10 * s);   // (the label kept off the tapes)
      g_ren.rect(lx - tw * 0.5f - 5 * s, gy + r + 3 * s, tw + 10 * s, 17 * s, vec3(0.01f, 0.02f, 0.04f), 0.65f, 4 * s);
      g_ren.text(lx, gy + r + 4 * s, 12 * s, t, gold, 1, 1);
      g_ren.uiBehindAircraft(false);
    } else {   // its bearing, along the top of the view
      vec2 d(dot(rel, fp.camRight), -dot(rel, fp.camUp));
      if (dot(rel, -fp.camBack) < 0) d = vec2(d.x >= 0 ? 1.f : -1.f, 0.f);
      const float dl = std::max(length(d), 1e-3f); d = vec2(d.x / dl, d.y / dl);
      const float ex = clampf(W * 0.5f + d.x * W * 0.4f, rail + 60 * s, W - rail - 60 * s), ey = band + 70 * s;
      hudDart(ex + d.x * 10 * s, ey, d.x >= 0 ? 1.f : -1.f, 0.f, 16 * s, 7 * s, gold, 0.9f);
      g_ren.text(ex - (d.x >= 0 ? 14 * s : -14 * s), ey - 7 * s, 12 * s, t, gold, 0.9f, d.x >= 0 ? 2 : 0);
    }
  }
  // ================================================================ the top band
  {
    hudStrip(0, 0, W, band, 1.f);
    // left: what this flight is, and the objective
    const char* kind = militaryFlight ? "MILITARY" : freeFlight ? "FREE FLIGHT" : researchFlight ? "RESEARCH" : contract.type == CT_LESSON ? "LESSON" : contract.type == CT_TRIAL ? "TRIAL" : contract.type == CT_FERRY ? "FREE FLIGHT" : "CONTRACT";
    vec3 kc = researchFlight ? vec3(0.75f, 0.45f, 1.f) : contract.type == CT_LESSON ? C_GOOD : C_ACCENT;
    g_ren.rect(0, 0, 3 * s, band, kc, 0.95f);
    float kw = g_ren.text(14 * s, 6 * s, 9.5f * s, kind, kc, 1, 0, false);
    float leftW = W * 0.5f - 230 * s - 14 * s;
    g_ren.text(14 * s + kw + 10 * s, 5 * s, 13 * s, ellipsize(contract.title, leftW - kw - 10 * s, 13 * s), C_TEXT, 1, 0, false);
    std::string obj = toWp ? fmt("CHECKPOINT %d / %d", wpIndex + 1, (int)contract.wps.size()) : fmt("LAND  %s  %s", d.code, d.name);
    if (freeFlight) {
      obj = "FREE ROAM / NO MISSION / CAREER UNCHANGED";
      if (earthDestination) obj = "EARTH ROUTE / " + earthDestination->name;
      if (earthFacilitiesPending) obj = earthPromotionDeferred ? "TERRAIN CONTACT PROTECTED / AIRPORTS PENDING" : "DETAIL PREPARING / LOCAL AIRPORTS UNAVAILABLE";
    }
    else if (researchFlight) obj = resCard >= 0 ? (resCardDone ? std::string(kResCards[resCard].id) + "  CARD COMPLETE" : fmt("%s  STEP %d/%d  %s", kResCards[resCard].id, resStep + 1, kResCards[resCard].n, kResCards[resCard].steps[std::min(resStep, kResCards[resCard].n - 1)].label)) : fmt("FREE ROAM  -  MACH %.2f", plane.mach);
    if(militaryFlight || (researchFlight && resCard<0 && specIdx==kSomnus)) obj=hiveObjectiveText();
    g_ren.text(14 * s, 24 * s, 12 * s, ellipsize(obj, leftW, 12 * s), mag, 1, 0, false);
    {   // the nav line under it: distance and time to the target
      std::string nav = fmt("%s   BRG %03.0f   ETE %s", dist < 1000.f ? fmt("%.0f m", dist).c_str() : set.metric ? fmt("%.1f km", dist / 1000.f).c_str() : fmt("%.1f nm", dist / 1852.f).c_str(), brg, gs > 10 ? fmt("%d:%02d", (int)(dist / gs) / 60, (int)(dist / gs) % 60).c_str() : "--:--");
      if (freeFlight && earthFlight) {
        nav = fmt("%+.3f LAT  %+.3f LON", earthPosition.latitude, earthPosition.longitude);
        if (earthDestination) {
          const auto bearing = regionTravel::Chart(flightRegion.origin).bearing(plane.pos, earthDestination->origin);
          nav = fmt("%.1f km  BRG %03.0f  %s", geo::surfaceDistance(earthPosition, earthDestination->origin) / 1000.,
            bearing.value_or(0.), earthCourseHold ? "HEADING HOLD" : "MANUAL");
        }
      }
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
    if (timeAccel > 1) extra += fmt("%sTIME WARP x%.0f", extra.empty() ? "" : "   ", timeAccel);
    if (researchFlight && resCard >= 0 && !resCardDone) { extra = resStepText(kResCards[resCard].steps[std::min(resStep, kResCards[resCard].n - 1)]) + (resHold > 0 ? fmt("  (%.0f s)", resHold) : ""); ec = C_ACCENT; }
    if (!extra.empty()) g_ren.text(rx, 40 * s, 11.5f * s, extra, ec, 1, 2, false);
  }
  // ================================================================ warnings: icons under the band, the edges lit
  bool stall = (plane.stallWarn > 0.8f && !plane.onGround && plane.ias > 10.f) || hudDemo;
  bool pullUp = (!plane.onGround && plane.agl() < 120 && plane.vel.y < -7.f) || hudDemo;
  bool gearW = gearWarning() || hudDemo;
  bool engOff = (!plane.engineRunning && engineAutoStarted && plane.starterTime <= 0 && !plane.glideOnly() && !(engineShutDown && plane.onGround)) || hudDemo;
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
      float ls = 10.5f * s;   // (the label inside its box: "ENGINE OFF" ran over the border)
      for (int k = 0; k < 6 && g_ren.textWidth(w.label, ls) > isz - 8 * s; k++) ls *= 0.92f;
      g_ren.text(x0 + isz * 0.5f, iy + isz - 1 * s, ls, w.label, w.col, a, 1, false);
      x0 += isz + gap;
    }
    if (engOff && n) {   // (on a dark backing: grey on a bright sky was unreadable)
      const std::string t = "press " + actLabel(ACT_ENGINE, padPrompts(), true) + " to restart";
      const float tw = g_ren.textWidth(t, 12 * s);
      g_ren.rect(W * 0.5f - tw * 0.5f - 10 * s, iy + isz + 20 * s, tw + 20 * s, 22 * s, vec3(0.03f, 0.f, 0.f), 0.75f, 4 * s);
      g_ren.text(W * 0.5f, iy + isz + 24 * s, 12 * s, t, C_TEXT, 0.95f, 1, false);
    }
  }
  // ================================================================ the rails
  float railTop = band + 10 * s, railBot = cockpit ? H - 60 * s : std::min(H - stripH - pfd - 18 * s, tilesTop - 30 * s), railH = railBot - railTop;
  if (!cockpit && railH > 160 * s) {
    // ---- left: the speed tape
    {
      float x = 0, mid = railTop + railH * 0.5f;
      float unit = set.metric ? 3.6f : MS_TO_KT; float v = plane.ias * unit, span = set.metric ? 120.f : 60.f, ppu = railH / (2.f * span);
      hudStrip(x, railTop, rail, railH, 0.9f);
      const PerfModel& P = Plane::perf(&spc);
      float vs = (plane.flaps > 0.5f ? P.vs0 : P.vs1) * unit * sqrtf(plane.mass() / std::max(spc.emptyMass + spc.designFuel() * 0.6f + 150.f, 1.f));
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
  // ================================================================ bottom-right: the systems tiles (not in the essentials)
  if (!cockpit && !set.hudEssential) {
    const AircraftSpec& sp = spc;
    struct Tile { std::string k, v; vec3 c; float bar; };   // bar < 0: none
    std::vector<Tile> tiles;
    tiles.push_back({"THR", fmt("%.0f%%", plane.ctl.throttle * 100), plane.ctl.throttle > 0.85f ? C_WARN : C_TEXT, plane.ctl.throttle});
    if (sp.engineType == ENG_PISTON) tiles.push_back({"RPM", plane.engineRunning ? fmt("%.0f", plane.rpm) : (plane.starterTime > 0 ? "CRANK" : "OFF"), plane.engineRunning ? C_TEXT : C_BAD, plane.engineRunning ? clampf(plane.rpm / sp.maxRpm, 0.f, 1.f) : 0.f});
    else tiles.push_back({"N1", plane.engineRunning ? fmt("%.1f%%", plane.n1) : (plane.starterTime > 0 ? fmt("ST %.0f", plane.n1) : "OFF"), plane.engineRunning ? C_TEXT : C_BAD, clampf(plane.n1 / 100.f, 0.f, 1.f)});
    // (the fuel as the hours and minutes of cruise it lasts - the tanks hold real fuel, and a short hop leaves with a part
    // load - red under twenty minutes; the bar its share of the tanks)
    float fuelFrac = plane.fuel / std::max(sp.maxFuel, 1.f);
    const int endur = (int)cruiseMinutes(sp, plane.fuel); const bool fuelLow = endur < 20;
    const std::string fuelS = fmt("%d:%02d", endur / 60, endur % 60);
    if (sp.special) { tiles.push_back({"FUEL", fuelS, fuelLow ? C_BAD : C_TEXT, fuelFrac}); tiles.push_back({"MACH", fmt("%.2f", plane.mach), plane.mach > sp.designMach * 0.95f ? C_WARN : C_TEXT, -1.f}); }
    else { tiles.push_back({"FUEL", fuelS, fuelLow ? C_BAD : C_TEXT, fuelFrac}); tiles.push_back({"RANGE", fmt("%.0f km", plane.rangeLeftKm()), fuelLow ? C_BAD : C_TEXT, -1.f}); }
    if (sp.special == 2) {
      const vec3 VIO(0.8f, 0.5f, 1.f);
      tiles.push_back({"PODS", fmt("%.0f deg%s", plane.nozzle * 90, plane.nozzle > 0.99f ? " VTOL" : ""), C_TEXT, plane.nozzle});
      tiles.push_back({"CLOAK", somnus.stealth > 0.99f ? "ACTIVE" : somnus.stealth > 0.01f ? fmt("%.0f%%", somnus.stealth * 100) : "OFF", somnus.stealth > 0.01f ? VIO : C_DIM, somnus.stealth});
      tiles.push_back({"WEAPONS", somnus.lasers > 0.97f ? "HOT" : somnus.lasers > 0.01f ? "DEPLOY" : "SAFE", somnus.lasers > 0.97f ? C_BAD : C_DIM, -1.f});
      tiles.push_back({"PLASMA", somnus.bay > 0.05f ? "BAY OPEN" : somnus.bombLoaded >= 1.f ? "READY" : "CHARGING", somnus.bombLoaded >= 1.f ? VIO : C_WARN, somnus.bombLoaded});
    } else if (sp.special == 3) {   // the XR-50: its engines' mode, its rocket, its leading edges, its wing and its nozzles
      const vec3 AMB(1.f, 0.72f, 0.3f);
      const float tb = plane.tbcc[0] + plane.tbcc[1] + plane.tbcc[2];
      const char* mode = !plane.engineRunning ? "OFF" : plane.tbcc[2] > 0.5f * tb && tb > 1.f ? "SCRAMJET" : plane.tbcc[1] > 0.5f * tb && tb > 1.f ? "RAMJET"
                       : plane.engineSpool > 0.86f && plane.nzSwivel < 5.f * DEG ? "REHEAT" : "TURBOFAN";
      tiles.push_back({"ENGINES", mode, plane.tbcc[2] > 0.5f * tb && tb > 1.f ? AMB : C_TEXT, -1.f});
      const float ox = plane.oxidizer / kMorpheusOxidizer;
      tiles.push_back({"ROCKET", plane.rocketN > 1.f ? fmt("LIT %.0f s", morpheusRocketSeconds(plane.oxidizer, plane.ctl.throttle)) : ox > 0.001f ? fmt("%.0f kg LOX", plane.oxidizer) : "SPENT",
                       plane.rocketN > 1.f ? AMB : ox > 0.001f ? C_DIM : C_BAD, ox});
      const float skinC = plane.skinT - 273.15f, skinK = (plane.skinT - 288.f) / (kMorpheusSkinLimit - 288.f);
      tiles.push_back({"LEAD EDGE", fmt("%.0f C", skinC), plane.skinT > kMorpheusSkinWarn ? C_BAD : plane.skinT > kMorpheusSkinWarn - 250.f ? C_WARN : C_TEXT, clampf(skinK, 0.f, 1.f)});
      tiles.push_back({"WING", fmt("%.0f deg%s", plane.sweep / DEG, plane.hiLift > 0.5f ? " FLAP" : ""), C_TEXT, (plane.sweep - kMorpheusSweepMin) / (kMorpheusSweepMax - kMorpheusSweepMin)});
      tiles.push_back({"NOZZLES", plane.fanClutch > 0.05f ? fmt("%.0f deg FAN", plane.nzSwivel / DEG) : fmt("%.0f deg", plane.nzSwivel / DEG), plane.fanClutch > 0.05f ? AMB : C_TEXT, plane.nozzle});
    } else if (sp.special) tiles.push_back({"TVC", fmt("%+.0f deg", -clampf(plane.ctl.pitch + plane.ctl.trim * 0.3f, -1, 1) * 0.5f / DEG), C_TEXT, -1.f});
    else tiles.push_back({"FLAPS", fmt("%.0f%%", plane.flaps * 100), plane.flaps > 0.01f ? C_ACCENT : C_TEXT, plane.flaps});
    std::string gearS = !sp.retract ? "FIXED" : plane.gear > 0.99f ? "DOWN" : plane.gear < 0.01f ? "UP" : "TRANSIT";
    tiles.push_back({"GEAR", gearS, !sp.retract ? C_DIM : plane.gear > 0.99f ? C_GOOD : plane.gear < 0.01f ? C_DIM : C_WARN, sp.retract ? plane.gear : -1.f});
    tiles.push_back({"TRIM", fmt("%+.0f", plane.ctl.trim * 100), C_TEXT, -1.f});
    int rows = ((int)tiles.size() + tileCols - 1) / tileCols;
    float bx = W - tileCols * tileW - (tileCols - 1) * tileGap - 14 * s, by = H - stripH - 10 * s - rows * tileH - (rows - 1) * tileGap;
    hudStrip(bx - 8 * s, by - 20 * s, tileCols * tileW + (tileCols - 1) * tileGap + 16 * s, rows * tileH + (rows - 1) * tileGap + 28 * s, 0.9f);
    g_ren.text(bx, by - 15 * s, 9.5f * s, sp.special == 3 ? "XR-50 SYSTEMS" : sp.special == 2 ? "XR-40 SYSTEMS" : sp.special ? "XR-30 SYSTEMS" : "SYSTEMS", C_ACCENT, 0.9f, 0, false);
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
      vec3 wv = plane.windAvg; float ws = length(vec3(wv.x, 0, wv.z)); float from = wrapDeg360(atan2f(-wv.x, wv.z) / DEG);
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
        if (!set.hudEssential || fabsf(xw) > 7.f) {   // (the essentials: only a strong crosswind's)
          g_ren.text(cxc, sy + 6 * s, 9 * s, "COMPONENTS", C_DIM, 1, 0, false);
          const std::string comp = fmt("%s %s   X %s %s", hw >= 0 ? "HEAD" : "TAIL", fmtSpeed(fabsf(hw)).c_str(), fmtSpeed(fabsf(xw)).c_str(), xw >= 0 ? "R" : "L");
          g_ren.text(cxc, sy + 17 * s, 12 * s, comp, fabsf(xw) > 7.f ? C_WARN : C_DIM, 1, 0, false);
          windEnd = std::max(windEnd, cxc + g_ren.textWidth(comp, 12 * s));
        }
      } else g_ren.text(58 * s, sy + 16 * s, 14 * s, "CALM", C_TEXT, 1, 0, false);
    }
    // right: g against the structure, then the mode chips and the autopilot's words
    float rx = W - 14 * s;
    if (!set.hudEssential || plane.gLoad > spc.gLimitPos() * 0.5f) {   // (the essentials: only when it matters)
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
      chip("LDG LT", C_ACCENT, landingLight); if (plane.onWater) chip("MOORED", C_WARN, plane.moored); else chip("BRAKE", C_WARN, plane.ctl.brake > 0.5f); chip("AP", APC, plane.apOn);
      if (plane.apOn) { std::string ap = plane.apStatus; float aw = g_ren.textWidth(ap, 11 * s); cx -= aw; g_ren.text(cx, sy + 17 * s, 11 * s, ap, APC, 1, 0, false); cx -= 10 * s; }
      rx = cx;
    }
    // centre: the instructor
    if (set.showHints && !hint.empty() && !crashed) {
      float hx0 = std::max(330 * s, windEnd + 24 * s), hx1 = rx - 24 * s, hw = hx1 - hx0;
      if (hw > 200 * s) {
        // two lines, or three a size smaller when a narrow strip (the autopilot's words beside it) needs them: the whole
        // hint, not one cut with "..."
        float fs = 12.5f * s, lh = 16 * s;
        auto lines = wrap(hint, hw - 70 * s, fs);
        if (lines.size() > 2) { fs = 11 * s; lh = 13 * s; lines = wrap(hint, hw - 70 * s, fs); }
        if (lines.size() > 3) { lines.resize(3); lines[2] = ellipsize(lines[2] + " ...", hw - 70 * s, fs); }
        g_ren.rect(hx0, sy + 6 * s, 3 * s, stripH - 12 * s, C_GOOD, 1);
        g_ren.text(hx0 + 12 * s, sy + 5 * s, 8.5f * s, "INSTRUCTOR", C_GOOD, 1, 0, false);
        float ly = sy + (lines.size() == 1 ? 22 * s : lines.size() == 2 ? 14 * s : 4 * s);
        for (auto& l : lines) { g_ren.text(hx0 + 64 * s, ly, fs, l, C_TEXT, 1, 0, false); ly += lh; }
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
  if(militaryFlight || (researchFlight && specIdx==kSomnus)) {
    if(specIdx==kSomnus) {
      const auto& gun=hive::forwardSpec(combatLoadout.forward);
      const auto& bomb=hive::bombSpec(combatLoadout.bomb);
      const float xx=rail+16*s, yy=band+32*s;
      g_ren.text(xx,yy,12*s,fmt("%s / %d RDS / HEAT %.0f%%%s",gun.name,combatLoadout.rounds,100*combatLoadout.heat,combatLoadout.overheated?" / COOLING":""),combatLoadout.overheated?C_WARN:gun.colour,1);
      g_ren.text(xx,yy+18*s,12*s,fmt("%s / %d STORES / HULL %.0f%%",bomb.name,combatLoadout.bombs,militaryHull),C_TEXT,1);
      if(gun.charge>0 && combatLoadout.charge>0) g_ren.text(xx,yy+36*s,12*s,fmt("CHARGING %.0f%%",100*combatLoadout.charge/gun.charge),gun.colour,1);
    }
    for(const auto& a:hiveCombat.actors) if(a.alive) {
      vec3 p=plane.pos+hiveCombat.displacement(plane.pos,a.position); float sx,sy;
      if(g_ren.project(fp,p,sx,sy) && sx>rail+10*s && sx<W-rail-10*s && sy>band+90*s && sy<H-120*s) {
        vec3 col=a.phase==hive::Phase::Telegraph?C_WARN:C_BAD;
        g_ren.rectOutline(sx-14*s,sy-14*s,28*s,28*s,col,.8f,0,1*s);
        const char* names[]={"NEEDLE","BASTION","CANTOR","ARCHON"};
        g_ren.text(sx,sy+18*s,10*s,fmt("%s %.1f km",names[(int)a.type],length(p-plane.pos)/1000),col,1,1,false);
      }
    }
  }
  // (the radio: under the tower recall on the left)
  if (showRadio) drawRadioPanel(cockpit ? 16 * s : rail + 12 * s, msgTop + 110 * s);
  else if (radio.state() == Radio::PLAYING && !stations.empty()) g_ren.text(cockpit ? 16 * s : rail + 12 * s, msgTop + 110 * s, 11 * s, "RADIO  " + stations[std::clamp(set.radioStation, 0, (int)stations.size() - 1)].first, C_DIM, 0.8f, 0, false);
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
  // (a place on the map at its copy nearest the aircraft - across the map's seam, the far side's; toR: a point given
  // relative to the aircraft as it is, the grid and the track ahead)
  auto toS = [&](float wx_, float wz) { return vec2(mcx + g_world.wrap(wx_ - plane.pos.x) * k, mcy + g_world.wrap(wz - plane.pos.z) * k); };
  auto toR = [&](float wx_, float wz) { return vec2(mcx + (wx_ - plane.pos.x) * k, mcy + (wz - plane.pos.z) * k); };
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
  for (float gx = floorf((plane.pos.x - range) / gstep) * gstep; gx <= plane.pos.x + range; gx += gstep) { vec2 a = toR(gx, plane.pos.z - range), b = toR(gx, plane.pos.z + range); seg(a, b, 1 * s, C_ACCENT, 0.12f); }
  for (float gz = floorf((plane.pos.z - range) / gstep) * gstep; gz <= plane.pos.z + range; gz += gstep) { vec2 a = toR(plane.pos.x - range, gz), b = toR(plane.pos.x + range, gz); seg(a, b, 1 * s, C_ACCENT, 0.12f); }
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
    vec2 a = toS(pts[i].x, pts[i].y), b(a.x + g_world.wrap(pts[i + 1].x - pts[i].x) * k, a.y + g_world.wrap(pts[i + 1].y - pts[i].y) * k);   // (each leg the short way)
    bool act = i == activeLeg, done = i < activeLeg;
    if (act) { a = C; b = toS(pts[i + 1].x, pts[i + 1].y); seg(a, b, 6 * s, MAG, 0.18f); dashed(a, b, 3 * s, MAG, 1.f, 10 * s, -T * 40 * s); }
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
  // landmarks: small diamonds, gold once found, named when the map is close in (the passport lists them all)
  for (const Site& st : profileSites()) {
    const vec2 p = toS(st.pos.x, st.pos.z); if (!inside(p, 6 * s)) continue;
    const bool got = profile.sites.count(st.id) > 0;
    const vec3 c = got ? vec3(1.f, 0.82f, 0.35f) : vec3(0.8f, 0.85f, 0.9f);
    const float r = 5 * s, al = got ? 1.f : 0.8f;
    seg(vec2(p.x - r, p.y), vec2(p.x, p.y - r), 2 * s, c, al); seg(vec2(p.x, p.y - r), vec2(p.x + r, p.y), 2 * s, c, al);
    seg(vec2(p.x + r, p.y), vec2(p.x, p.y + r), 2 * s, c, al); seg(vec2(p.x, p.y + r), vec2(p.x - r, p.y), 2 * s, c, al);
    if (range < 12000.f && inside(p, 60 * s)) g_ren.text(p.x + r + 4 * s, p.y - 7 * s, 11 * s, st.name, c, al * e, 0, true);
  }
  // airports: runway to scale (min length on screen), code labels, destination pulse
  int nearest = g_world.nearestAirport(plane.pos.x, plane.pos.z);
  for (int i = 0; i < (int)g_world.airports.size(); i++) {
    const Airport& a = g_world.airports[i];
    vec2 p = toS(a.x, a.z); if (!inside(p, -40 * s)) continue;
    vec3 dir = a.dir(); float hl = std::max(a.length * 0.5f * k, 6 * s);
    bool isDest = &a == &dest();
    vec3 c = isDest ? MAG : i == nearest ? C_WARN : a.water() ? vec3(0.45f, 0.8f, 1.f) : vec3(0.9f, 0.95f, 1.f);
    if (a.water()) {   // a seaplane base: its water lane dashed between the end buoys, a ring at its middle
      for (int d = 0; d < 3; d++) {
        const float t0 = -1.f + d * 0.8f, t1 = t0 + 0.4f;
        seg(vec2(p.x + dir.x * hl * t0, p.y + dir.z * hl * t0), vec2(p.x + dir.x * hl * t1, p.y + dir.z * hl * t1), 2.5f * s, c, 1.f);
      }
      g_ren.rectOutline(p.x - 4 * s, p.y - 4 * s, 8 * s, 8 * s, c, 1.f, 4 * s, 1.5f * s);
    } else {
      seg(vec2(p.x - dir.x * hl, p.y - dir.z * hl), vec2(p.x + dir.x * hl, p.y + dir.z * hl), 4 * s, vec3(0.05f, 0.05f, 0.07f), 0.9f);
      seg(vec2(p.x - dir.x * hl, p.y - dir.z * hl), vec2(p.x + dir.x * hl, p.y + dir.z * hl), 2 * s, c, 1.f);
    }
    if (isDest) for (int ring = 0; ring < 2; ring++) { float ph = fmodf(T * 0.7f + ring * 0.5f, 1.f), r = (8 + 30 * ph) * s; if (inside(p, r)) g_ren.rectOutline(p.x - r, p.y - r, 2 * r, 2 * r, MAG, (1 - ph) * e, r, 2 * s); }
    if (inside(p, 30 * s)) g_ren.text(p.x + 8 * s, p.y + 4 * s, 12 * s, a.code, c, e, 0, true);
    if (i == apDest) {   // autoland pick: cyan target ring
      float r = 14 * s + 2 * s * sinf(T * 4.f);
      if (inside(p, r)) { g_ren.glow(p.x - r, p.y - r, 2 * r, 2 * r, AP_CYAN, 0.3f * e, r, 8 * s); g_ren.rectOutline(p.x - r, p.y - r, 2 * r, 2 * r, AP_CYAN, e, r, 2.5f * s); }
      if (inside(p, 30 * s)) g_ren.text(p.x + 8 * s, p.y - 16 * s, 11 * s, "AUTOLAND", AP_CYAN, e, 0, true);
    }
    // click an airport to pick it for the autopilot
    if (in.mPressed[0] && !pointerOverOverlay() && inside(p, 0) && fabsf(in.mx - p.x) < 16 * s && fabsf(in.my - p.y) < 16 * s && apDest != i) { apDest = i; g_audio.trigger(SFX_CLICK); }
  }
  // the real Earth's airports' other runways, to scale beside their main ones
  for (const ExtraRunway& r : g_world.extraRunways) {
    const vec2 p = toS(r.x, r.z); if (!inside(p, -40 * s)) continue;
    const float h = r.heading * DEG, hl = std::max(r.length * 0.5f * k, 4 * s);
    const vec2 d(sinf(h), -cosf(h));
    const vec3 c = &g_world.airports[r.airport] == &dest() ? MAG : r.airport == nearest ? C_WARN : vec3(0.9f, 0.95f, 1.f);
    seg(vec2(p.x - d.x * hl, p.y - d.y * hl), vec2(p.x + d.x * hl, p.y + d.y * hl), 4 * s, vec3(0.05f, 0.05f, 0.07f), 0.9f);
    seg(vec2(p.x - d.x * hl, p.y - d.y * hl), vec2(p.x + d.x * hl, p.y + d.y * hl), 2 * s, c, 1.f);
  }
  // the real Earth: its airports beyond this chart (the next charts' own, there once flown into), dimmer, the larger
  // named, every one once the map is close in
  for (const BeyondAirport& b : beyondAirports()) {
    const vec2 p = toR(b.pos.x, b.pos.y); if (!inside(p, -40 * s)) continue;
    const float hl = std::max(b.length * 0.5f * k, 5 * s);
    const vec3 c = b.size >= 1 ? vec3(0.78f, 0.84f, 0.9f) : vec3(0.62f, 0.68f, 0.74f);
    seg(vec2(p.x - b.dir.x * hl, p.y - b.dir.y * hl), vec2(p.x + b.dir.x * hl, p.y + b.dir.y * hl), 3.5f * s, vec3(0.05f, 0.05f, 0.07f), 0.7f);
    seg(vec2(p.x - b.dir.x * hl, p.y - b.dir.y * hl), vec2(p.x + b.dir.x * hl, p.y + b.dir.y * hl), 1.6f * s, c, 0.8f);
    if ((b.size >= 1 || range < 15000.f) && inside(p, 30 * s)) g_ren.text(p.x + 7 * s, p.y + 3 * s, 11 * s, b.ident, c, 0.8f * e, 0, true);
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
    vec2 p = toR(plane.pos.x + gv.x * 60 * m, plane.pos.z + gv.y * 60 * m);
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
  vec3 to = target - plane.pos; to.x = g_world.wrap(to.x); to.z = g_world.wrap(to.z);   // (the short way round the map)
  float dist = length(vec3(to.x, 0, to.z));
  float brg = wrapDeg360(atan2f(to.x, -to.z) / DEG);
  float gs = length(vec3(plane.vel.x, 0, plane.vel.z));
  float trk = gs > 2 ? wrapDeg360(atan2f(plane.vel.x, -plane.vel.z) / DEG) : plane.heading();
  // remaining route distance
  float remain = dist;
  { vec2 prev(target.x, target.z); for (int i = wpIndex + 1; i < (int)contract.wps.size(); i++) { vec2 p(contract.wps[i].x, contract.wps[i].z); remain += length(vec3(g_world.wrap(p.x - prev.x), 0, g_world.wrap(p.y - prev.y))); prev = p; }
    if (wpIndex < (int)contract.wps.size()) remain += length(vec3(g_world.wrap(dest().x - prev.x), 0, g_world.wrap(dest().z - prev.y))); }
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
  g_ren.text(px, py, 13 * s, ellipsize(plane.apOn ? plane.apStatus : "OFF  -  " + actLabel(ACT_AP, padPrompts(), true) + " engages", vw, 13 * s), plane.apOn ? C_GOOD : C_DIM, e); py += 20 * s;
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
  if (freeFlight && freeRegionMap) { drawRegionPicker(); return; }
  if (freeFlight && freeEarthPicker) { drawEarthAirportPicker(); return; }
  if (militaryFlight && !settingsFromPause) { drawMilitaryPause(); return; }
  float s = S(), W = (float)g_ren.W, H = (float)g_ren.H;
  g_ren.rect(0, 0, W, H, vec3(0, 0, 0), 0.55f);
  if (settingsFromPause) {
    float pw = std::min((settingsPage >= 1 ? 940 : 760) * s, W - 40 * s);
    panel(W * 0.5f - pw * 0.5f, 60 * s, pw, H - 120 * s, 0.95f);
    drawSettings(W * 0.5f - pw * 0.5f + 24 * s, 80 * s, pw - 48 * s, H - 160 * s - (settingsPage >= 1 ? 0 : 50 * s));
    if (settingsPage == 0 && button(W * 0.5f + pw * 0.5f - 180 * s, H - 120 * s - 58 * s, 150 * s, 42 * s, "Back", true, true)) settingsFromPause = false;
    if (settingsPage >= 1 && button(W * 0.5f - pw * 0.5f + 24 * s, H - 120 * s - 4 * s - 48 * s, 150 * s, 36 * s, "Back", true, true)) { settingsFromPause = false; bindCapture = -1; axisCapture = -1; }
    return;
  }
  const bool combatPractice=researchFlight && resCard<0 && specIdx==kSomnus;
  s = std::min(s, std::min(H / (combatPractice?880.f:freeFlight?660.f:600.f), W / 520.f));
  float pw = 410 * s, ph = (combatPractice?798.f:freeFlight?568.f:508.f) * s, x = W * 0.5f - pw * 0.5f, y = H * 0.5f - ph * 0.5f;
  panel(x, y, pw, ph, 0.95f);
  g_ren.text(x + 30 * s, y + 24 * s, 11 * s, "FLIGHT OPERATIONS / ON HOLD", C_ACCENT, 1, 0, false);
  g_ren.text(x + 30 * s, y + 49 * s, 32 * s, "Flight paused", C_TEXT, 1);
  float by = y + 112 * s, bw = 350 * s, bh = 46 * s;
  if (button(x + 30 * s, by, bw, bh, "Resume", true, true)) paused = false;
  by += bh + 12 * s;
  if (button(x + 30 * s, by, bw * 0.48f, bh, "Restart flight")) restartFlight();
  if (button(x + 30 * s + bw * 0.52f, by, bw * 0.48f, bh, "Report a problem")) {   // (what was happening, beside the logs: the playtest kit)
    const std::string r = writeProblemReport();
    if (r.empty()) toast("Couldn't write the report (the save folder isn't writable)", vec3(1.f, 0.45f, 0.35f), false);
    else { toast("Report written beside the logs: describe what happened, then send it", vec3(0.6f, 1.f, 0.7f), false); if (platformOpen) platformOpen(r); }
  }
  by += bh + 12 * s;
  if (button(x + 30 * s, by, bw * 0.48f, bh, "Settings")) { settingsFromPause = true; settingsPage = 0; }
  if (button(x + 30 * s + bw * 0.52f, by, bw * 0.48f, bh, "Controls")) { settingsFromPause = true; settingsPage = 1; }
  by += bh + 12 * s;
  if (button(x + 30 * s, by, bw * 0.48f, bh, showRadio ? "Hide radio" : "Radio")) showRadio = !showRadio;
  if (button(x + 30 * s + bw * 0.52f, by, bw * 0.48f, bh, uiHidden ? "Show flight UI" : "Hide flight UI")) uiHidden = !uiHidden;   // (also LB+RB held)
  by += bh + 12 * s;
  if (button(x + 30 * s, by, bw, bh, in.pad && !set.padBind[ACT_PHOTO] ? std::string("Photo mode") : "Photo mode   " + actLabel(ACT_PHOTO, in.pad, true))) { enterPhoto(); return; }
  by += bh + 12 * s;
  if (freeFlight) {
    if (realEarthPlay()) {   // (the real Earth: a route to any of its airports)
      if (button(x + 30 * s, by, bw, bh, "Fly to a real airport / Set route")) { freeEarthPicker = true; earthPick = 0; earthQuery.clear(); earthResultsFor = "\x01"; return; }
    } else if (button(x + 30 * s, by, bw, bh, "Earth destinations / Set route")) { openEarthPicker(); return; }
    by += bh + 12 * s;
  }
  if (arrivalAt >= 0 && !researchFlight) {   // stopped at a field after landing: parking there ends the flight as the brake would
    if (button(x + 30 * s, by, bw, bh, arrivalAt == contract.to ? "Finish flight here" : "End the flight here", true, true)) { parkingBrake = true; paused = false; }
  } else if (button(x + 30 * s, by, bw, bh, freeFlight ? "Return to Free Flight" : researchFlight ? "End research flight" : "Abandon flight")) endFlight(false, researchFlight ? "" : "Abandoned flight", OUT_ABANDONED);
  if(combatPractice) drawCombatPractice(x+30*s,by+bh+18*s,bw);
  if (showRadio) drawRadioPanel(20 * s, 60 * s);
}

// The touchdown's grade, stamped in the upper middle of the view for under three seconds: the word on the
// settlement's scale (career.h touchdownGrade) and the rate under it
void Game::drawLandingStamp() {
  if (tdStampT < 0.f || screen != SCR_FLIGHT || crashed) return;
  const float age = realTime - tdStampT;
  if (age < 0.f || age > 2.8f) return;
  const float s = S(), W = (float)g_ren.W, H = (float)g_ren.H;
  const int g = touchdownGrade(tdStampFpm);
  const vec3 col = g == TD_BUTTER ? vec3(1.f, 0.82f, 0.35f) : g <= TD_GOOD ? C_GOOD : g == TD_FIRM ? C_WARN : C_BAD;
  const float in = smoothstepf(0.f, 0.16f, age), a = in * (1.f - smoothstepf(2.2f, 2.8f, age));
  const float fs = 46 * s * (1.f + 0.35f * (1.f - in));   // (it lands: large, then settling)
  const char* word = touchdownGradeWord(g);
  const std::string sub = fmt("%.0f fpm", fabsf(tdStampFpm));
  const float bw = std::max(g_ren.textWidth(word, fs), g_ren.textWidth(sub, 16 * s)) + 64 * s, bh = fs + 44 * s;
  const float x0 = W * 0.5f - bw * 0.5f, y0 = H * 0.28f - bh * 0.5f;
  g_ren.glow(x0, y0, bw, bh, col, 0.35f * a, 10 * s, 26 * s);
  g_ren.rect(x0, y0, bw, bh, vec3(0.01f, 0.02f, 0.04f), 0.72f * a, 10 * s);
  g_ren.rectOutline(x0, y0, bw, bh, col, 0.8f * a, 10 * s, 2 * s);
  g_ren.text(W * 0.5f, y0 + 8 * s, fs, word, col, a, 1);
  g_ren.text(W * 0.5f, y0 + bh - 28 * s, 16 * s, sub, C_TEXT, a, 1, false);
}

// An achievement as it is earned: a card sliding down at the top centre, its chime as it arrives (one at a time)
void Game::drawAchievementBanner() {
  if (achShow.empty()) return;
  AchShow& a = achShow.front();
  const Achievement* ac = findAchievement(a.id);
  if (!ac) { achShow.erase(achShow.begin()); return; }
  g_ren.flushUIPublic();   // (over everything drawn before it, its text too)
  if (!a.played) { a.played = true; a.t = 0.f; if (g_music.owns()) g_music.sting(STING_ACHIEVEMENT); else g_audio.trigger(SFX_CHIME, 0.7f); }
  const float s = S(), W = (float)g_ren.W, t = a.t;
  const float in = clampf(t / 0.35f, 0.f, 1.f), out = clampf((4.5f - t) / 0.4f, 0.f, 1.f), k = std::min(in * in * (3 - 2 * in), out);
  const float w = std::min(460 * s, W - 40 * s), h = 64 * s, x = W * 0.5f - w * 0.5f, y0 = screen == SCR_FLIGHT ? (float)g_ren.H * 0.17f : 130 * s, y = -h + (h + y0) * k;   // (in flight under the heading tape; elsewhere under the tabs)
  g_ren.glow(x, y, w, h, C_ACCENT, 0.35f * k, 8 * s, 18 * s);
  g_ren.rect(x, y, w, h, vec3(0.02f, 0.035f, 0.05f), 0.95f * k, 8 * s);
  g_ren.rect(x, y, w, 2 * s, C_ACCENT, k);
  // the medal: a ring with a star-ish centre, turning in
  const float cx = x + 34 * s, cy = y + h * 0.5f, r = 20 * s;
  g_ren.rect(cx - r, cy - r, r * 2, r * 2, C_ACCENT, 0.9f * k, r);
  g_ren.rect(cx - r * 0.72f, cy - r * 0.72f, r * 1.44f, r * 1.44f, vec3(0.05f, 0.06f, 0.07f), k, r * 0.72f);
  g_ren.text(cx, cy - 11 * s, 20 * s, "*", vec3(1.f, 0.88f, 0.5f), k, 1);
  g_ren.text(x + 66 * s, y + 10 * s, 11 * s, "ACHIEVEMENT UNLOCKED", C_ACCENT, k, 0, false);
  fitText(x + 66 * s, y + 25 * s, w - 82 * s, 18 * s, 12 * s, ac->title, C_TEXT, k);
  fitText(x + 66 * s, y + 46 * s, w - 82 * s, 12 * s, 9 * s, ac->desc, C_DIM, k);
}

// The licence card over the hub: what the new licence opens up, the aircraft first
void Game::drawLicenceCard() {
  const float s = S(), W = (float)g_ren.W, H = (float)g_ren.H;
  static float opened = -1.f; static int openedFor = -1;
  if (openedFor != licenceShown) { openedFor = licenceShown; opened = realTime; }
  const float e = realTime - opened, k = clampf(e / 0.4f, 0.f, 1.f), ease = 1.f - (1.f - k) * (1.f - k);
  g_ren.rect(0, 0, W, H, vec3(0, 0, 0), 0.55f * ease);
  const float pw = std::min(620 * s, W - 40 * s), ph = std::min(470 * s, H - 40 * s), x = W * 0.5f - pw * 0.5f, y = H * 0.5f - ph * 0.5f + (1.f - ease) * 30 * s;
  g_ren.glow(x, y, pw, ph, vec3(1.f, 0.82f, 0.35f), 0.25f * ease, 12 * s, 30 * s);
  g_ren.rect(x, y, pw, ph, svui::ink, 1, 6 * s);   // (opaque: the hub stays under it)
  panel(x, y, pw, ph, 0.97f);
  // the wings: two bars either side of a circle
  const float cx = x + pw * 0.5f, cy = y + 62 * s;
  for (int sd = -1; sd <= 1; sd += 2)
    for (int i = 0; i < 4; i++) {
      const float len = (120 - i * 22) * s * ease, bx = sd < 0 ? cx - 34 * s - len : cx + 34 * s;
      g_ren.rect(bx, cy - 14 * s + i * 8 * s, len, 4 * s, vec3(1.f, 0.82f, 0.35f), 0.9f - i * 0.15f, 2 * s);
    }
  g_ren.rect(cx - 28 * s, cy - 28 * s, 56 * s, 56 * s, vec3(1.f, 0.82f, 0.35f), ease, 28 * s);
  g_ren.rect(cx - 22 * s, cy - 22 * s, 44 * s, 44 * s, vec3(0.06f, 0.05f, 0.03f), ease, 22 * s);
  const char* abbr[] = {"SP", "PPL", "CPL", "ATP"};
  g_ren.text(cx, cy - 9 * s, 16 * s, abbr[std::clamp(licenceShown, 0, 3)], vec3(1.f, 0.85f, 0.45f), ease, 1);
  float py = y + 108 * s;
  g_ren.text(cx, py, 12 * s, "LICENCE AWARDED", C_ACCENT, ease, 1, false); py += 22 * s;
  std::string nm = licenseName(licenceShown); std::string up; for (char ch : nm) up += (char)toupper((unsigned char)ch);
  { float fs = 30 * s; while (fs > 16 * s && g_ren.textWidth(up, fs) > pw - 60 * s) fs -= 1 * s; g_ren.text(cx, py, fs, up, C_TEXT, ease, 1); }
  py += 48 * s;
  header(x + 30 * s, py, pw - 60 * s, "NOW OPEN TO YOU"); py += 28 * s;
  std::vector<std::string> lines;
  for (int i = 0; i < kAircraftCount; i++) if (kAircraft[i].license == licenceShown && !kAircraft[i].special) lines.push_back(std::string(kAircraft[i].name) + "  -  " + kAircraft[i].role);
  const char* work[] = {"", "Rentals and cargo work across the islands", "Passenger flights, and aircraft of your own to buy or finance", "Your own airline: routes, hired pilots, the biggest aircraft"};
  if (licenceShown >= 1 && licenceShown <= 3) lines.push_back(work[licenceShown]);
  if (licenceShown >= LIC_ATP) lines.push_back("An invitation from Project NIGHTGLASS: its research terminal, on the main menu");
  int n = 0;
  for (auto& l : lines) {
    if (py > y + ph - 90 * s) break;
    const float a = clampf((e - 0.35f - n++ * 0.08f) / 0.25f, 0.f, 1.f);
    g_ren.rect(x + 34 * s, py + 6 * s, 6 * s, 6 * s, C_GOOD, a, 3 * s);
    fitText(x + 50 * s, py, pw - 80 * s, 15 * s, 11 * s, l, C_TEXT, a);
    py += 26 * s;
  }
  if (button(x + pw * 0.5f - 110 * s, y + ph - 66 * s, 220 * s, 46 * s, "Continue", true, true) || in.pressed[K_ENTER] || in.pressed[K_ESC]) { licenceShown = -1; g_audio.trigger(SFX_CLICK); }
}

// A trial's result over the hub: the score, the medal and its marks, the local board, the gate splits against the
// best run's; Retry flies it again in the same aircraft
void Game::drawTrialCard() {
  TrialResult& R = trialResult;
  const float s = S(), W = (float)g_ren.W, H = (float)g_ren.H;
  static float opened = -1.f; static const TrialResult* was = nullptr; static float scoreWas = -2.f;
  if (was != &R || scoreWas != R.score || opened < 0.f) { was = &R; scoreWas = R.score; opened = realTime; }
  const float e = realTime - opened, k = clampf(e / 0.35f, 0.f, 1.f), ease = 1.f - (1.f - k) * (1.f - k);
  g_ren.rect(0, 0, W, H, vec3(0, 0, 0), 0.55f * ease);
  const bool splits = !R.splits.empty();
  steam::BoardScore world; const bool worldwide = !R.board.empty() && steam::boardScore(R.board, world);   // (on Steam: the run's worldwide place)
  const float pw = std::min(620 * s, W - 40 * s), ph = std::min(((splits ? 600.f : 440.f) + (worldwide ? 24.f : 0.f)) * s, H - 40 * s), x = W * 0.5f - pw * 0.5f, y = H * 0.5f - ph * 0.5f;
  const vec3 mc = R.medal == 3 ? vec3(1.f, 0.82f, 0.3f) : R.medal == 2 ? vec3(0.82f, 0.87f, 0.92f) : R.medal == 1 ? vec3(0.86f, 0.56f, 0.3f) : C_DIM;
  if (R.medal > 0) g_ren.glow(x, y, pw, ph, mc, 0.25f * ease, 12 * s, 30 * s);
  g_ren.rect(x, y, pw, ph, svui::ink, 1, 6 * s);   // (opaque: the hub stays under it)
  panel(x, y, pw, ph, 0.97f);
  const float cx = x + pw * 0.5f;
  float py = y + 24 * s;
  g_ren.text(cx, py, 12 * s, "TRIAL RESULT", C_ACCENT, ease, 1, false); py += 20 * s;
  { std::string t = trialName(R.kind); std::string up; for (char ch : t) up += (char)toupper((unsigned char)ch);
    g_ren.text(cx, py, 22 * s, up + "  -  " + kAircraft[std::clamp(R.spec, 0, kAircraftCount - 1)].name, C_TEXT, ease, 1); }
  py += 42 * s;
  // the medal
  const float mr = 34 * s * (0.7f + 0.3f * clampf((e - 0.2f) / 0.3f, 0.f, 1.f));
  if (R.medal > 0) {
    g_ren.glow(cx - mr, py, mr * 2, mr * 2, mc, 0.5f * ease, mr, 18 * s);
    g_ren.rect(cx - mr, py, mr * 2, mr * 2, mc, ease, mr);
    g_ren.rect(cx - mr * 0.74f, py + mr * 0.26f, mr * 1.48f, mr * 1.48f, mc * 0.5f, ease, mr * 0.74f);
    const char* nm[] = {"", "BRONZE", "SILVER", "GOLD"};
    g_ren.text(cx, py + mr - 8 * s, 15 * s, nm[R.medal], vec3(1, 1, 1), ease, 1);
  } else {
    hudRing(cx, py + mr, mr, 2 * s, C_DIM, 0.6f * ease, 48);
    g_ren.text(cx, py + mr - 8 * s, 13 * s, R.score >= 0 ? "NO MEDAL" : "DNF", C_DIM, ease, 1);
  }
  py += 34 * s * 2 + 14 * s;
  // the score and the board
  g_ren.text(cx, py, 30 * s, trialScore(R.kind, R.score), R.score >= 0 ? C_TEXT : C_BAD, ease, 1); py += 40 * s;
  if (R.score < 0) { fitText(x + 30 * s, py, pw - 60 * s, 14 * s, 11 * s, R.why, C_BAD, ease); py += 26 * s; }
  else if (R.newBest) { g_ren.text(cx, py, 15 * s, "A NEW BEST ON THIS BOARD!", vec3(1.f, 0.85f, 0.45f), ease, 1); py += 26 * s; }
  else { g_ren.text(cx, py, 14 * s, fmt("Rank %d on the board  -  best %s", R.rank, trialScore(R.kind, R.best).c_str()), C_DIM, ease, 1); py += 26 * s; }
  { float m[3]; trialMedalMarks(R.kind, R.spec, m);
    fitText(x + 30 * s, py, pw - 60 * s, 13 * s, 10 * s, fmt("Gold %s    Silver %s    Bronze %s", trialScore(R.kind, m[0]).c_str(), trialScore(R.kind, m[1]).c_str(), trialScore(R.kind, m[2]).c_str()), C_DIM, ease);
    py += 30 * s; }
  if (worldwide) {
    const int of = std::max(world.total, world.rank);
    const std::string t = world.done ? (world.improved ? fmt("Worldwide on Steam: #%d of %d", world.rank, of) : fmt("Worldwide on Steam: your best stands at #%d of %d", world.rank, of))
                        : world.failed ? std::string("Worldwide on Steam: the score couldn't be sent") : std::string("Worldwide on Steam: sending the score...");
    g_ren.text(cx, py, 14 * s, t, world.done ? C_ACCENT : C_DIM, ease, 1); py += 24 * s;
  }
  if (splits) {   // the gates: this run's time at each, and against the best run's (before this one)
    header(x + 30 * s, py, pw - 60 * s, "SPLITS"); py += 28 * s;
    const int n = (int)R.splits.size(), half = (n + 1) / 2;
    const float colW = (pw - 60 * s) * 0.5f;
    for (int i = 0; i < n; i++) {
      const float rx = x + 30 * s + (i >= half ? colW : 0.f), ry = py + (i % half) * 22 * s;
      const float a = clampf((e - 0.4f - i * 0.06f) / 0.2f, 0.f, 1.f);
      g_ren.text(rx, ry, 13 * s, fmt("Gate %d", i + 2), C_DIM, a);
      g_ren.text(rx + 80 * s, ry, 13 * s, trialScore(TR_GATES, R.splits[i]), C_TEXT, a);
      if (i < (int)R.bestSplits.size() && R.bestSplits[i] > 0.f) {
        const float d = R.splits[i] - R.bestSplits[i];
        g_ren.text(rx + colW - 20 * s, ry, 13 * s, fmt("%+.1f s", d), d <= 0.f ? C_GOOD : C_BAD, a, 2);
      }
    }
    py += half * 22 * s + 10 * s;
  }
  const float by = y + ph - 66 * s;
  if (button(x + 30 * s, by, 200 * s, 46 * s, "Retry") || (in.buttonsPressed & PAD_Y)) {
    const TrialResult r = R; R.kind = -1;
    startTrial(r.kind, r.spec, r.src);
    return;
  }
  if (button(x + pw - 230 * s, by, 200 * s, 46 * s, "Done", true, true) || in.pressed[K_ENTER] || in.pressed[K_ESC]) { R.kind = -1; g_audio.trigger(SFX_CLICK); }
}

// Photo mode's panel (game_photo.cpp), at the right: the lens, the light, the colour, the frame and the looks, the
// gates shown or not, the shot. Hidden (H, or Y), only a line says how to bring it back. A shot flashes the screen and
// names its file for a moment
void Game::photoPanelRect(float& x, float& y, float& w, float& h) const {
  const float s = S();
  w = std::min(380 * s, g_ren.W * 0.45f); h = std::min(600 * s, g_ren.H - 40 * s);
  x = g_ren.W - w - 20 * s; y = 20 * s;
}

void Game::drawPhotoPanel() {
  const float s = S(), W = (float)g_ren.W, H = (float)g_ren.H;
  const float since = realTime - photo.savedT;
  if (since >= 0.f && since < 0.35f) { const float k = 1.f - since / 0.35f; g_ren.rect(0, 0, W, H, vec3(1, 1, 1), 0.6f * k * k); }
  if (since >= 0.f && since < 3.5f) {
    const std::string msg = photo.last.empty() ? std::string("The photo couldn't be saved") : "Saved: " + photo.last + (steam::active() ? "   (and to Steam)" : "");
    const float a = 1.f - smoothstepf(2.8f, 3.5f, since), tw = std::min(g_ren.textWidth(msg, 14 * s), W - 80 * s);
    g_ren.rect(W * 0.5f - tw * 0.5f - 14 * s, H - 66 * s, tw + 28 * s, 30 * s, svui::ink, 0.8f * a, 6 * s);
    g_ren.text(W * 0.5f, H - 59 * s, 14 * s, ellipsize(msg, tw, 14 * s), photo.last.empty() ? C_BAD : C_GOOD, a, 1, false);
  }
  if (!photo.panel) {
    const std::string hint = in.pad ? padGlyph("Y") + " the panel      " + padGlyph("X") + " take the photo      " + padGlyph("B") + " leave" : "H  the panel      Space  take the photo      Esc  leave";
    g_ren.text(24 * s, H - 34 * s, 13 * s, hint, C_TEXT, 0.55f, 0, true);
    return;
  }
  float x, y, w, h; photoPanelRect(x, y, w, h);
  panel(x, y, w, h, 0.9f);
  const float lx = x + 20 * s, cw = w - 40 * s;
  float py = y + 20 * s;
  g_ren.text(lx, py, 11 * s, "PHOTO MODE", C_ACCENT, 1, 0, false); py += 18 * s;
  g_ren.text(lx, py, 22 * s, ellipsize(plane.spec ? plane.spec->name : "", cw, 22 * s), C_TEXT, 1); py += 38 * s;
  // a row: its name, a track to drag (or step with the D-pad's left and right once it has the focus), its value
  auto slider = [&](const char* label, float& v, float lo, float hi, float step, const std::string& disp) {
    const float tx = lx + 108 * s, tw = cw - 108 * s - 64 * s, ty = py + 6 * s;
    g_ren.text(lx, py + 2 * s, 13 * s, label, C_DIM, 1);
    const bool hv = hovered(tx - 6 * s, py, tw + 12 * s, 24 * s);
    const float f = clampf((v - lo) / (hi - lo), 0.f, 1.f);
    g_ren.rect(tx, ty + 4 * s, tw, 4 * s, C_ACCENT, 0.15f, 2 * s);
    g_ren.rectGrad(tx, ty + 4 * s, std::max(tw * f, 4 * s), 4 * s, C_ACCENT * 0.6f, C_ACCENT, 1, 2 * s);
    g_ren.rect(tx + tw * f - 6 * s, ty, 12 * s, 12 * s, hv ? mixc(C_ACCENT, vec3(1, 1, 1), 0.3f) : C_ACCENT, 1, 6 * s);
    if (hv && in.mDown[0]) v = lo + clampf((in.mx - tx) / tw, 0.f, 1.f) * (hi - lo);
    int st = 0; focusAdjust(uid(tx, py, std::string("photo:") + label), tx - 6 * s, py, tw + 12 * s, 24 * s, &st);
    if (st) v = clampf(v + st * step, lo, hi);
    g_ren.text(lx + cw, py + 2 * s, 13 * s, disp, C_TEXT, 1, 2);
    py += 32 * s;
  };
  header(lx, py, cw, "LENS AND LIGHT"); py += 26 * s;
  slider("Field of view", photo.fov, 15.f, 100.f, 5.f, fmt("%.0f deg", photo.fov));
  slider("Exposure", photo.ev, -2.f, 2.f, 0.1f, fmt("%+.1f EV", photo.ev));
  slider("Vignette", photo.vignette, 0.f, 1.5f, 0.1f, fmt("%.0f%%", photo.vignette / 1.5f * 100.f));
  header(lx, py, cw, "COLOUR"); py += 26 * s;
  slider("Saturation", photo.saturation, 0.f, 1.6f, 0.05f, photo.saturation < 0.01f ? std::string("Mono") : fmt("%.0f%%", photo.saturation * 100.f));
  slider("Contrast", photo.contrast, 0.7f, 1.4f, 0.05f, fmt("%.0f%%", photo.contrast * 100.f));
  slider("Warmth", photo.warmth, -1.f, 1.f, 0.05f, fabsf(photo.warmth) < 0.01f ? std::string("Neutral") : fmt("%s %.0f", photo.warmth > 0 ? "Warm" : "Cool", fabsf(photo.warmth) * 100.f));
  {   // the looks: each sets the colour and the vignette at once
    static const char* looks[5] = {"Natural", "Vivid", "Film", "Noir", "Golden"};
    const float bw = (cw - 4 * 6 * s) / 5.f;
    for (int i = 0; i < 5; i++) if (button(lx + i * (bw + 6 * s), py, bw, 28 * s, std::string(looks[i]) + "##look", true, photo.look == i)) photoLook(i);
    py += 40 * s;
  }
  header(lx, py, cw, "FRAME"); py += 26 * s;
  {
    static const char* frames[4] = {"Full", "2.39 : 1", "1.85 : 1", "Square"};
    const float bw = (cw - 3 * 6 * s) / 4.f;
    for (int i = 0; i < 4; i++) if (button(lx + i * (bw + 6 * s), py, bw, 28 * s, std::string(frames[i]) + "##frame", true, photo.frame == i)) photo.frame = i;
    py += 36 * s;
    if (button(lx, py, cw, 28 * s, photo.markers ? "Gates and route: shown##markers" : "Gates and route: hidden##markers", true, photo.markers)) photo.markers = !photo.markers;
    py += 40 * s;
  }
  if (button(lx, py, cw, 40 * s, in.pad ? "Take the photo   " + padGlyph("X") : std::string("Take the photo   (Space)"), true, true)) photo.capture = 1;
  py += 48 * s;
  const float hw = (cw - 8 * s) * 0.5f;
  if (button(lx, py, hw, 30 * s, "Reset camera")) {
    const float size = plane.spec ? std::max(plane.spec->fusLen, plane.spec->span) : 10.f;
    photo.pos = plane.pos - plane.forward() * (size * 1.4f + 6.f) + vec3(0, size * 0.3f + 2.f, 0);
    const vec3 f = normalize(plane.pos - photo.pos);
    photo.yaw = atan2f(f.x, -f.z); photo.pitch = asinf(clampf(f.y, -1.f, 1.f)); photo.roll = 0.f; photo.fov = set.fov;
  }
  if (button(lx + hw + 8 * s, py, hw, 30 * s, "Leave photo mode")) { exitPhoto(); return; }
  py += 40 * s;
  const std::string help = in.pad ? "Left stick moves, triggers lower and raise, right stick turns, bumpers tilt. D-pad picks a setting. " + padGlyph("Y") + " hides the panel, " + padGlyph("B") + " leaves."
                                  : "W A S D move, Q E lower and raise (Shift faster), drag to turn, [ ] tilt, the wheel zooms. H hides the panel, Esc leaves.";
  for (auto& l : wrap(help, cw, 12 * s)) { if (py > y + h - 16 * s) break; g_ren.text(lx, py, 12 * s, l, C_DIM, 0.85f, 0, false); py += 16 * s; }
}

// The demo's end (Game::demo): the private licence earned - what the full game goes on to, that the career carries
// over, and the way to the store page
void Game::drawDemoEnd() {
  const float s = S(), W = (float)g_ren.W, H = (float)g_ren.H;
  g_ren.rect(0, 0, W, H, vec3(0, 0, 0), 0.6f);
  const float pw = std::min(660 * s, W - 40 * s), ph = std::min(380 * s, H - 40 * s), x = W * 0.5f - pw * 0.5f, y = H * 0.5f - ph * 0.5f;
  g_ren.glow(x, y, pw, ph, C_ACCENT, 0.28f, 12 * s, 30 * s);
  g_ren.rect(x, y, pw, ph, svui::ink, 1, 6 * s);
  panel(x, y, pw, ph, 0.97f);
  float py = y + 28 * s;
  g_ren.text(x + pw * 0.5f, py, 12 * s, "THE END OF THE DEMO", C_ACCENT, 1, 1, false); py += 24 * s;
  g_ren.text(x + pw * 0.5f, py, 30 * s, "Private Pilot", C_TEXT, 1, 1); py += 50 * s;
  const char* body = "You've earned your wings. In the full game the career goes on: the commercial and airline licences, the "
                     "whole fleet up to the 180-seat Galleon, an airline of your own with hired pilots, the legendary contracts, "
                     "and the XR research jets for Defense Command.";
  for (auto& l : wrap(body, pw - 60 * s, 15 * s)) { g_ren.text(x + 30 * s, py, 15 * s, l, C_TEXT, 0.95f); py += 21 * s; }
  py += 12 * s;
  for (auto& l : wrap("Your career, your achievements and your pilot profile carry over to the full game.", pw - 60 * s, 14 * s)) { g_ren.text(x + 30 * s, py, 14 * s, l, C_GOOD, 1); py += 20 * s; }
  const int n = SOMNIUM_STORE_APPID ? 3 : 2;   // (the Wishlist button only with the full game's app id)
  const float bw = std::min(200 * s, (pw - 60 * s - (n - 1) * 10 * s) / n), by = y + ph - 66 * s;
  float bx = x + pw * 0.5f - (n * bw + (n - 1) * 10 * s) * 0.5f;
  if (n == 3) { if (button(bx, by, bw, 46 * s, "Wishlist on Steam", true, true)) openStorePage(); bx += bw + 10 * s; }
  if (button(bx, by, bw, 46 * s, "Free flight")) { beginFreeFlightSetup(); return; }
  if (button(bx + bw + 10 * s, by, bw, 46 * s, "Main menu") || in.pressed[K_ESC] || (in.buttonsPressed & PAD_B)) { screen = SCR_MENU; g_audio.trigger(SFX_CLICK); }
}

void Game::openStorePage() {
  if (!SOMNIUM_STORE_APPID) return;
  if (!steam::openStore(SOMNIUM_STORE_APPID) && platformOpen) platformOpen(fmt("https://store.steampowered.com/app/%d/", (int)SOMNIUM_STORE_APPID));
}

// The credits, from the main menu: they roll up by themselves (the wheel or the right stick takes them by hand); Esc, B,
// Enter or Close end them. Who made the game, then the work it stands on (THIRD_PARTY_NOTICES.md has the licences)
void Game::drawCredits() {
  const float s = S(), W = (float)g_ren.W, H = (float)g_ren.H;
  if (in.pressed[K_ESC] || in.pressed[K_ENTER] || (in.buttonsPressed & (PAD_B | PAD_A))) { showCredits = false; g_audio.trigger(SFX_CLICK); return; }
  g_ren.rect(0, 0, W, H, svui::ink, 1.f);
  struct Line { float size; const char* text; int kind; };   // kind 0 a heading, 1 a name, 2 a note, 3 space
  static const Line lines[] = {
    {12, "SOMNIUM VOLANDI", 0}, {40, "The dream of flight", 1}, {16, "", 3},
    {12, "A GAME BY", 0}, {24, "CDAIII", 1}, {16, "", 3},
    {12, "FLIGHT, WORLD, CAREER, MUSIC AND SOUND", 0}, {17, "Made for this game, in code: the flight model, the islands and their towns,", 2},
    {17, "the weather, the aircraft, the campaign, the score and every sound but the voices", 2}, {16, "", 3},
    {12, "VOICES", 0}, {17, "Synthesised for the game with Qwen3-TTS (Apache 2.0)", 2}, {17, "and Kokoro-82M (Apache 2.0) through kokoro-onnx (MIT)", 2}, {16, "", 3},
    {12, "SURFACES", 0}, {17, "Photographed materials from Poly Haven and ambientCG (CC0)", 2},
    {17, "Sergej Majboroda, Jenelle van Heerden, Dimitrios Savva, Rico Cilliers, Rob Tuytel, Amal Kumar", 2}, {16, "", 3},
    {12, "THE NIGHT SKY", 0}, {17, "The Yale Bright Star Catalogue, D. Hoffleit and W. H. Warren Jr.", 2}, {16, "", 3},
    {12, "TYPE", 0}, {17, "DejaVu Sans, from Bitstream Vera", 2}, {16, "", 3},
    {12, "IMAGES", 0}, {17, "stb_image, Sean Barrett", 2}, {16, "", 3},
    {12, "EARTH DATA TOOLS", 0}, {17, "NOAA ETOPO 2022 relief; OurAirports, its contributors", 2}, {16, "", 3},
    {12, "THE FULL LICENCES", 0}, {17, "THIRD_PARTY_NOTICES.md, beside the game", 2}, {30, "", 3},
    {28, "Thank you for flying.", 1},
  };
  float total = 0; for (const Line& l : lines) total += (l.kind == 3 ? l.size : l.size + 14) * s;
  float& scroll = creditsScroll;   // (each opening from the bottom: the first lines rise into view)
  if (in.wheel != 0) { scroll += -in.wheel * 60.f * s; creditsByHand = true; in.wheel = 0; }   // (by hand: the rolling stops)
  if (in.pad && fabsf(in.ry) > 0.2f) { scroll -= in.ry * 400.f * s * uiDt; creditsByHand = true; }
  if (!creditsByHand) scroll += 38.f * s * uiDt;
  scroll = std::clamp(scroll, 0.f, total + H * 0.5f);
  float py = H - scroll;
  for (const Line& l : lines) {
    const float sz = l.size * s;
    if (l.kind == 3) { py += sz; continue; }
    if (py > -60 * s && py < H + 20 * s) {
      const float fade = clampf(std::min(py / (H * 0.15f), (H - py) / (H * 0.15f)), 0.f, 1.f);
      const vec3 col = l.kind == 0 ? C_ACCENT : l.kind == 1 ? C_TEXT : C_DIM;
      g_ren.text(W * 0.5f, py, sz, l.text, col, fade, 1, l.kind == 1);
    }
    py += sz + 14 * s;
  }
  // (the button and its hint in the top corners, where the roll has faded out: at the foot the widest lines ran under them)
  if (button(W - 170 * s, 24 * s, 140 * s, 40 * s, "Close", true, true)) { showCredits = false; return; }
  g_ren.text(30 * s, 38 * s, 12 * s, padPrompts() ? padGlyph("B") + "  CLOSE" : std::string("ESC  CLOSE"), C_DIM, 0.8f, 0, false);
}

// The first start: what to call the pilot, how to fly (assisted or realistic), and the road from the first lesson to
// the XR jets. Seen once (profile.welcomed); everything on it can be changed later in Settings
void Game::drawWelcome() {
  const float s = S(), W = (float)g_ren.W, H = (float)g_ren.H;
  g_ren.rect(0, 0, W, H, vec3(0, 0, 0), 0.6f);
  const float pw = std::min(640 * s, W - 40 * s), ph = std::min(560 * s, H - 40 * s), x = W * 0.5f - pw * 0.5f, y = H * 0.5f - ph * 0.5f;
  g_ren.glow(x, y, pw, ph, C_ACCENT, 0.25f, 12 * s, 30 * s);
  g_ren.rect(x, y, pw, ph, svui::ink, 1, 6 * s);
  panel(x, y, pw, ph, 0.97f);
  float py = y + 26 * s;
  g_ren.text(x + pw * 0.5f, py, 12 * s, "WELCOME ABOARD", C_ACCENT, 1, 1, false); py += 22 * s;
  g_ren.text(x + pw * 0.5f, py, 28 * s, "Somnium Volandi", C_TEXT, 1, 1); py += 40 * s;
  fitText(x + 30 * s, py, pw - 60 * s, 14 * s, 11 * s, "The dream of flight: from your first lesson to an airline of your own.", C_DIM); py += 34 * s;
  // the name: typed, or picked
  header(x + 30 * s, py, pw - 60 * s, "WHAT SHOULD THE TOWER CALL YOU?"); py += 28 * s;
  for (char ch : in.text) if (profile.name.size() < 16 && (isalnum((unsigned char)ch) || ch == ' ' || ch == '-' || ch == '\'')) profile.name += ch;
  if (in.pressed[K_BACK] && !profile.name.empty()) profile.name.pop_back();
  const float fw = pw - 60 * s - 170 * s;
  // (on a Steam Deck or in Big Picture: Steam's floating keyboard over the field, when the card shows and on a tap or X)
  if (!steamKeyboardAsked || (in.mPressed[0] && hovered(x + 30 * s, py, fw, 38 * s)) || (in.buttonsPressed & PAD_X)) {
    steamKeyboardAsked = true;
    steam::showKeyboard((int)(x + 30 * s), (int)py, (int)fw, (int)(38 * s));
  }
  g_ren.rect(x + 30 * s, py, fw, 38 * s, vec3(0.02f, 0.04f, 0.06f), 1, 4 * s);
  g_ren.rectOutline(x + 30 * s, py, fw, 38 * s, C_ACCENT, 0.5f, 4 * s, s);
  const std::string shown = profile.name.empty() ? std::string("Type a name") : profile.name;
  const float tw = g_ren.text(x + 42 * s, py + 9 * s, 18 * s, shown, profile.name.empty() ? C_DIM : C_TEXT, profile.name.empty() ? 0.6f : 1.f);
  if (!profile.name.empty() && fmodf(realTime, 1.f) < 0.55f) g_ren.rect(x + 44 * s + tw, py + 8 * s, 2 * s, 22 * s, C_ACCENT, 1);
  if (button(x + pw - 30 * s - 160 * s, py, 160 * s, 38 * s, "Surprise me")) {
    static const char* names[] = {"Kestrel", "Ada", "Juno", "Wren", "Maverick", "Skye", "Atlas", "Nova", "Falco", "Iris", "Vega", "Orion"};
    static int k = (int)(realTime * 7.f); profile.name = names[(k++) % 12];
  }
  py += 56 * s;
  // how to fly
  header(x + 30 * s, py, pw - 60 * s, "HOW WOULD YOU LIKE TO FLY?"); py += 28 * s;
  const float bw = (pw - 60 * s - 12 * s) * 0.5f;
  const int cur = welcomeStyle >= 0 ? welcomeStyle : (set.assist == 0 ? 1 : 0);
  if (button(x + 30 * s, py, bw, 40 * s, "Assisted", true, cur == 0)) { welcomeStyle = 0; set.assist = 1; set.air = 0; }
  if (button(x + 30 * s + bw + 12 * s, py, bw, 40 * s, "Realistic", true, cur == 1)) { welcomeStyle = 1; set.assist = 0; set.air = 1; }
  py += 48 * s;
  fitText(x + 30 * s, py, pw - 60 * s, 12.5f * s, 10 * s, cur == 0 ? "Let go of the controls and the aircraft holds its bank and its climb; the air is calm." : "Every input moves the controls directly, and the air is as the forecast says.", C_DIM);
  py += 20 * s;
  fitText(x + 30 * s, py, pw - 60 * s, 12.5f * s, 10 * s, "You can change this any time in Settings > Flying.", C_DIM * 0.85f);
  py += 34 * s;
  header(x + 30 * s, py, pw - 60 * s, "THE ROAD AHEAD"); py += 28 * s;
  const char* road[] = {"Student", "Private", "Commercial", "Airline", "XR jets"};
  const float rw = (pw - 60 * s) / 5.f;
  for (int i = 0; i < 5; i++) {
    const float cx = x + 30 * s + rw * (i + 0.5f);
    if (i < 4) g_ren.rect(cx, py + 9 * s, rw, 2 * s, C_ACCENT, 0.35f);
    g_ren.rect(cx - 9 * s, py, 18 * s, 18 * s, i == 0 ? C_ACCENT : svui::ink, 1, 9 * s);
    g_ren.rectOutline(cx - 9 * s, py, 18 * s, 18 * s, C_ACCENT, 0.8f, 9 * s, s);
    g_ren.text(cx, py + 26 * s, 12 * s, road[i], i == 0 ? C_TEXT : C_DIM, 1, 1);
  }
  if (button(x + pw * 0.5f - 120 * s, y + ph - 66 * s, 240 * s, 46 * s, "Let's fly", true, true) || in.pressed[K_ENTER]) {
    // (a new pilot gets the style shown; a career already under way keeps its settings unless one was picked)
    if (welcomeStyle < 0 && !hasSave) { if (cur == 0) { set.assist = std::max(set.assist, 1); set.air = 0; } else { set.assist = 0; set.air = 1; } }
    profile.welcomed = true; saveProfile(); saveSettings(); g_audio.trigger(SFX_CLICK);
  }
}

// The Military board before the Commercial licence: what it is, and what clears a pilot for it
void Game::drawMilitaryLocked() {
  const float s = S(), W = (float)g_ren.W, H = (float)g_ren.H;
  if (in.pressed[K_ESC] || (in.buttonsPressed & PAD_B)) { in.pressed[K_ESC] = false; hubList = 0; return; }
  g_ren.rect(0, 0, W, H, vec3(0.01f, 0.012f, 0.015f), 0.92f);
  const float pw = std::min(620 * s, W - 40 * s), ph = std::min(400 * s, H - 40 * s), x = W * 0.5f - pw * 0.5f, y = H * 0.5f - ph * 0.5f;
  g_ren.rect(x, y, pw, ph, svui::ink, 1, 6 * s);
  panel(x, y, pw, ph, 0.97f);
  float py = y + 28 * s;
  g_ren.text(x + pw * 0.5f, py, 12 * s, "DEFENSE COMMAND  //  RESTRICTED", C_WARN, 1, 1, false); py += 24 * s;
  g_ren.text(x + pw * 0.5f, py, 26 * s, "The XR programme", C_TEXT, 1, 1); py += 46 * s;
  for (auto& l : wrap("Funded sorties in the research jets - reconnaissance, interceptions, strikes - flown for Defense Command, "
                      "with a service record of their own. Defense Command takes pilots with some flying behind them.", pw - 60 * s, 14 * s)) {
    g_ren.text(x + 30 * s, py, 14 * s, l, C_DIM, 1); py += 20 * s;
  }
  py += 16 * s;
  if (demo) {   // (the demo: a teaser)
    header(x + 30 * s, py, pw - 60 * s, "IN THE FULL GAME"); py += 30 * s;
    g_ren.text(x + 30 * s, py, 15 * s, "The XR jets, Defense Command and the research terminal", C_TEXT, 1);
    const float bx = SOMNIUM_STORE_APPID ? x + pw * 0.5f - 230 * s : x + pw * 0.5f - 110 * s;   // (centred without the Wishlist button)
    if (button(bx, y + ph - 66 * s, 220 * s, 46 * s, "Back to work") || in.pressed[K_ENTER]) { hubList = 0; g_audio.trigger(SFX_CLICK); }
    if (SOMNIUM_STORE_APPID && button(x + pw * 0.5f + 10 * s, y + ph - 66 * s, 220 * s, 46 * s, "Wishlist on Steam", true, true)) openStorePage();
    return;
  }
  header(x + 30 * s, py, pw - 60 * s, "CLEARANCE"); py += 30 * s;
  g_ren.text(x + 30 * s, py, 15 * s, fmt("Flights in your logbook:  %d of %d", career.flights, kMilitaryFlights), C_TEXT, 1); py += 26 * s;
  g_ren.rect(x + 30 * s, py, pw - 60 * s, 4 * s, C_DIM, 0.2f, 2 * s);
  g_ren.rect(x + 30 * s, py, std::max(2 * s, (pw - 60 * s) * std::min(1.f, career.flights / (float)kMilitaryFlights)), 4 * s, C_WARN, 1, 2 * s); py += 18 * s;
  g_ren.text(x + 30 * s, py, 13 * s, "Every lesson and job you fly counts.", C_DIM, 1);
  if (button(x + pw * 0.5f - 110 * s, y + ph - 66 * s, 220 * s, 46 * s, "Back to work", true, true) || in.pressed[K_ENTER]) { hubList = 0; g_audio.trigger(SFX_CLICK); }
}

// The campaign's end: the career in numbers, and what is still out there
void Game::drawFinaleCard() {
  const float s = S(), W = (float)g_ren.W, H = (float)g_ren.H;
  static float opened = -1.f;
  if (opened < 0.f || realTime - opened > 1e5f) opened = realTime;
  const float e = realTime - opened, k = clampf(e / 0.6f, 0.f, 1.f), ease = 1.f - (1.f - k) * (1.f - k);
  g_ren.rect(0, 0, W, H, vec3(0, 0, 0), 0.6f * ease);
  const float pw = std::min(680 * s, W - 40 * s), ph = std::min(560 * s, H - 40 * s), x = W * 0.5f - pw * 0.5f, y = H * 0.5f - ph * 0.5f;
  g_ren.glow(x, y, pw, ph, vec3(1.f, 0.82f, 0.35f), 0.3f * ease, 14 * s, 36 * s);
  g_ren.rect(x, y, pw, ph, svui::ink, 1, 6 * s);   // (opaque: the hub stays under it)
  panel(x, y, pw, ph, 0.97f);
  float py = y + 28 * s;
  g_ren.text(x + pw * 0.5f, py, 12 * s, "THE SOMNIUM VOLANDI CAMPAIGN", C_ACCENT, ease, 1, false); py += 24 * s;
  { std::string nm = "CAPTAIN"; if (!profile.name.empty()) { nm += ' '; for (char ch : profile.name) nm += (char)toupper((unsigned char)ch); }
    g_ren.text(x + pw * 0.5f, py, 34 * s, nm, vec3(1.f, 0.85f, 0.45f), ease, 1); py += 50 * s; }
  fitText(x + 30 * s, py, pw - 60 * s, 15 * s, 11 * s, "From your first lesson to the last flight of the night: the islands are yours.", C_TEXT, ease); py += 34 * s;
  const std::string vals[] = {fmt("%d", career.flights), fmt("%.1f h", career.hours), fmt("%d", career.landings),
                              career.bestLandingFpm < 9000 ? fmt("%.0f fpm", career.bestLandingFpm) : std::string("-"), fmtMoney(career.money),
                              fmt("%d / %d", (int)profile.ach.size(), kAchievementCount)};
  const char* labs[] = {"FLIGHTS", "FLIGHT TIME", "LANDINGS", "SOFTEST LANDING", "BANK", "ACHIEVEMENTS"};
  const float cw = (pw - 60 * s - 24 * s) / 3.f;
  for (int i = 0; i < 6; i++) {
    const float a = clampf((e - 0.4f - i * 0.12f) / 0.3f, 0.f, 1.f);
    const float bx = x + 30 * s + (i % 3) * (cw + 12 * s), by = py + (i / 3) * 74 * s;
    g_ren.rect(bx, by, cw, 62 * s, C_PANEL2, 0.65f * a, 4 * s);
    g_ren.text(bx + 12 * s, by + 9 * s, 10 * s, labs[i], C_DIM, a, 0, false);
    fitText(bx + 12 * s, by + 28 * s, cw - 24 * s, 21 * s, 12 * s, vals[i], C_TEXT, a);
  }
  py += 160 * s;
  header(x + 30 * s, py, pw - 60 * s, "STILL OUT THERE"); py += 28 * s;
  const std::string more[] = {"Legendary contracts on the board: the hardest flying in the islands", "Your airline: more routes, better pilots, the Galleon 180",
                              fmt("%d of %d airports stamped, %d landmarks found in the islands; %d regions beyond them explored", profile.islandAirports(), solaceAirportCount(), profile.islandSites(), (int)profile.regionsExplored().size()),
                              "Trials for gold, the Military board, and Free Flight anywhere on Earth"};
  for (auto& l : more) {
    if (py > y + ph - 90 * s) break;
    g_ren.rect(x + 34 * s, py + 6 * s, 6 * s, 6 * s, C_ACCENT, ease, 3 * s);
    fitText(x + 50 * s, py, pw - 80 * s, 15 * s, 11 * s, l, C_TEXT, ease);
    py += 26 * s;
  }
  if (button(x + pw * 0.5f - 110 * s, y + ph - 66 * s, 220 * s, 46 * s, "Fly on", true, true) || in.pressed[K_ENTER] || in.pressed[K_ESC]) { finaleShown = false; opened = -1.f; g_audio.trigger(SFX_CLICK); }
}

void Game::drawDebrief() {
  if (isMilitaryContract(contract.type)) { drawMilitaryDebrief(); return; }
  float s = S(), W = (float)g_ren.W, H = (float)g_ren.H;
  // (it comes in: the title, then the lines one after another, the total counting up to what was earned)
  const float e = realTime - debriefT0;
  auto fadeIn = [&](float at) { const float k = clampf((e - at) / 0.22f, 0.f, 1.f); return k * k * (3 - 2 * k); };
  const float inK = clampf(e / 0.3f, 0.f, 1.f), inEase = 1.f - (1.f - inK) * (1.f - inK);   // (the panel rises in over the scene)
  g_ren.rect(0, 0, W, H, vec3(0, 0, 0), 0.35f * inEase);
  float pw = std::min(720 * s, W - 40 * s), ph = std::min(760 * s, H - 40 * s), x = W * 0.5f - pw * 0.5f, y = H * 0.5f - ph * 0.5f + (1.f - inEase) * 28 * s;
  panel(x, y, pw, ph, 0.95f);
  float px = x + 30 * s, py = y + 24 * s;
  for (auto& l : wrap(debriefTitle, pw - 60 * s, 28 * s)) { g_ren.text(px, py, 28 * s, l, lastSuccess ? C_GOOD : C_BAD, 1); py += 34 * s; }
  py += 8 * s;
  g_ren.text(px, py, 18 * s, ellipsize(contract.title, pw - 60 * s, 18 * s), C_TEXT, 1); py += 34 * s;
  const bool showStars = lastSuccess && contract.type != CT_FERRY, showMark = lastSuccess && lastMark.score >= 0;
  if (showStars || showMark) {
    float mx = px;
    if (showStars) {
      for (int i = 0; i < 3; i++) {
        float pop = anim(uid(px + i, py, "star"), 1.f, 6.f - i * 1.5f);
        float sz = 32 * s * (i < stars ? pop : 1.f), cx = px + i * 44 * s + 16 * s, cy = py + 16 * s;
        if (i < stars) g_ren.glow(cx - sz * 0.5f, cy - sz * 0.5f, sz, sz, C_WARN, 0.5f * pop, sz * 0.5f, 12 * s);
        g_ren.rect(cx - sz * 0.5f, cy - sz * 0.5f, sz, sz, i < stars ? C_WARN : vec3(0.12f, 0.16f, 0.2f), 1, sz * 0.5f);
        g_ren.rect(cx - sz * 0.22f, cy - sz * 0.22f, sz * 0.44f, sz * 0.44f, i < stars ? vec3(1, 0.95f, 0.8f) : vec3(0.2f, 0.25f, 0.3f), 1, sz * 0.22f);
      }
      mx = px + 150 * s;
    }
    if (showMark) {   // the landing's mark: a letter tile, the score out of 100 and what made it (landingMark)
      const char L = lastMark.letter;
      const vec3 lc = L == 'S' ? vec3(1.f, 0.82f, 0.3f) : L == 'A' ? C_GOOD : L == 'B' ? vec3(0.45f, 0.85f, 1.f) : L == 'C' ? C_WARN : C_BAD;
      const float pop = 0.6f + 0.4f * fadeIn(0.15f), ts = 40 * s * pop, tx = mx + 20 * s - ts * 0.5f, ty = py + 16 * s - ts * 0.5f;
      if (L == 'S') g_ren.glow(tx, ty, ts, ts, lc, 0.6f * fadeIn(0.15f), 10 * s, 16 * s);
      g_ren.rect(tx, ty, ts, ts, lc * 0.25f, 0.95f, 8 * s);
      g_ren.rect(tx, ty, ts, 2 * s, lc, 0.9f);
      g_ren.text(mx + 20 * s, py + 16 * s - 14 * s * pop, 28 * s * pop, std::string(1, L), lc, fadeIn(0.15f), 1);
      const float a = fadeIn(0.25f);
      g_ren.text(mx + 50 * s, py - 1 * s, 12 * s, fmt("LANDING  %d / 100", lastMark.score), C_DIM, a, 0, false);
      fitText(mx + 50 * s, py + 17 * s, x + pw - 30 * s - (mx + 50 * s), 13 * s, 10 * s,
              fmt("touchdown %d  zone %d  centreline %d  approach %d", lastMark.rate, lastMark.zone, lastMark.line, lastMark.approach), C_TEXT * 0.8f);
    }
    py += 48 * s;
  }
  // a new best for the type, and the achievements earned on this flight
  if (lastSuccess && pbNew && result.landed) {
    const std::string t = fmt("NEW PERSONAL BEST IN THE %s: %.0f FPM (WAS %.0f)", plane.spec ? plane.spec->name : "", fabsf(touchdownFpm), pbOld);
    std::string up; for (char ch : t) up += (char)toupper((unsigned char)ch);
    const float tw = std::min(g_ren.textWidth(up, 12 * s) + 24 * s, pw - 60 * s), a = fadeIn(0.35f);
    g_ren.rect(px, py, tw, 24 * s, vec3(1.f, 0.82f, 0.3f) * 0.22f, 0.9f * a, 5 * s);
    g_ren.text(px + 12 * s, py + 6 * s, 12 * s, ellipsize(up, tw - 24 * s, 12 * s), vec3(1.f, 0.85f, 0.4f), a, 0, false);
    py += 32 * s;
  }
  if (!flightAch.empty()) {
    float cx = px; int k = 0;
    for (const std::string& id : flightAch) {
      const Achievement* ac = findAchievement(id);
      if (!ac) continue;
      const std::string t = std::string("* ") + ac->title;
      const float tw = g_ren.textWidth(t, 13 * s) + 22 * s;
      if (cx + tw > x + pw - 30 * s && cx > px) { cx = px; py += 30 * s; }
      const float a = fadeIn(0.45f + 0.12f * k++);
      g_ren.rect(cx, py, tw, 24 * s, C_ACCENT * 0.25f, 0.9f * a, 12 * s);
      g_ren.text(cx + 11 * s, py + 5 * s, 13 * s, t, C_ACCENT, a);
      cx += tw + 8 * s;
    }
    py += 34 * s;
  }
  // The foot of the panel, from the buttons up, stays put: the unsaved notice, the campaign's end, a new licence and
  // the total. What is above it - the flight data, the coaching and the settlement's lines - scrolls (mouse wheel, the
  // right stick) in the room that is left, as one body: at a large UI scale on a small screen that room may hold only
  // a few lines (the review of v3.24.0, R9, and of v3.31.0, U4: the total's divider ran through the first fee)
  const bool campaignEnd = career.finished && lastSuccess && contract.story && contract.id == g_story.back().id;
  const auto campaignLines = campaignEnd ? wrap("You've completed the Somnium Volandi campaign. Congratulations, Captain!", pw - 60 * s, 18 * s) : std::vector<std::string>();
  const bool newLic = career.license > licenseBefore;
  const Contract* upNext = lastSuccess && !campaignEnd && !commitBlocked() && career.atHome() ? career.nextStory() : nullptr;   // (the campaign: in the islands)
  const float campY = (commitBlocked() ? y + ph - 108 * s : y + ph - 76 * s) - campaignLines.size() * 24 * s - (upNext ? 26 * s : 0.f);
  const float licY = campY - (newLic ? 34 * s : 0.f), totalY = licY - 34 * s;
  int total = 0;
  for (auto& l : payout) total += l.amount;
  const float bodyTop = py, bodyBot = std::max(bodyTop + 24 * s, totalY - 12 * s);
  static float payScroll = 0.f, bodyH = 0.f; static std::string payFor;
  const std::string payKey = debriefTitle + "|" + contract.id + "|" + std::to_string(payout.size()) + "|" + std::to_string(total);
  if (payKey != payFor) { payFor = payKey; payScroll = 0.f; }
  const float payMax = std::max(0.f, bodyH - (bodyBot - bodyTop));
  bool payWheeled = false;
  if (hovered(x, bodyTop, pw, bodyBot - bodyTop) && in.wheel != 0) { payScroll -= in.wheel * 48.f * s; in.wheel = 0; payWheeled = true; }
  payScroll = std::clamp(payScroll, 0.f, payMax);
  const size_t payFocus0 = focusList.size();
  g_ren.uiClip(x, bodyTop - 2 * s, x + pw, bodyBot);
  py = bodyTop - payScroll;
  // (only whole rows: one cut in half by the clip's edge ran under the total's divider)
  auto whole = [&](float h) { return py >= bodyTop - 2 * s && py + h <= bodyBot + 1 * s; };
  int rowN = 0;
  auto rowA = [&]() { return fadeIn(0.3f + 0.05f * rowN++); };
  auto row = [&](const std::string& k, const std::string& v) { const float a = rowA(); if (whole(20 * s)) { g_ren.text(px, py, 16 * s, k, C_DIM, a); g_ren.text(px + 230 * s, py, 16 * s, ellipsize(v, pw - 290 * s, 16 * s), C_TEXT, a); } py += 24 * s; };
  if (whole(20 * s)) header(px, py, pw - 60 * s, "FLIGHT DATA");
  py += 24 * s;
  if (result.landed) row("Touchdown", fmt("%.0f fpm  (%s)", touchdownFpm, touchdownGradeWord(touchdownGrade(touchdownFpm))));
  row("Flight time", fmt("%d:%02d", (int)flightClock / 60, (int)flightClock % 60));
  row("Max G", fmt("%.2f", plane.maxG));
  row("Max bank", fmt("%.0f deg", result.maxBank));
  row("Fuel used", fmt("%.0f kg", result.fuelUsedKg));
  if (!coaching.empty()) {   // one coaching point from the arrival (Game::landingCoaching)
    auto cl = wrap(coaching, pw - 60 * s - 30 * s, 15 * s);
    if (cl.size() > 2) { cl.resize(2); cl[1] = ellipsize(cl[1] + " ...", pw - 90 * s, 15 * s); }
    py += 4 * s;
    const float a = rowA();
    g_ren.rect(px, py, 3 * s, cl.size() * 20 * s, C_ACCENT, 0.8f * a);
    for (auto& l : cl) { if (whole(18 * s)) g_ren.text(px + 12 * s, py, 15 * s, l, C_ACCENT, 0.95f * a); py += 20 * s; }
  }
  py += 10 * s;
  if (whole(20 * s)) header(px, py, pw - 60 * s, "SETTLEMENT");
  py += 24 * s;
  for (auto& l : payout) {
    const float a = rowA();
    if (whole(20 * s)) {
      float vw = g_ren.text(x + pw - 30 * s, py, 16 * s, isMilitaryContract(contract.type)?fmt("%d SC",l.amount):fmtMoney(l.amount), l.amount >= 0 ? C_GOOD : C_BAD, a, 2);
      g_ren.text(px, py, 16 * s, ellipsize(l.label, pw - 80 * s - vw, 16 * s), C_TEXT, a);
    }
    py += 24 * s;
  }
  bodyH = py + payScroll - bodyTop;
  focusScrollPixels(payFocus0, uid(0, 0, "debrief-scroll"), x, bodyTop, pw, bodyBot, payScroll, payWheeled, payMax > 0.f);
  g_ren.uiClipOff();
  if (payMax > 0.f) {   // a thin bar at the panel's edge: where the view is in the body
    const float trackH = bodyBot - bodyTop, barH = std::max(20 * s, trackH * trackH / (trackH + payMax));
    g_ren.rect(x + pw - 18 * s, bodyTop, 3 * s, trackH, C_ACCENT, 0.12f);
    g_ren.rect(x + pw - 18 * s, bodyTop + (trackH - barH) * payScroll / payMax, 3 * s, barH, C_ACCENT, 0.7f);
  }
  {   // the total counts up once the lines are in
    const float k = clampf((e - 0.3f - 0.05f * rowN) / 0.9f, 0.f, 1.f), ease = 1.f - (1.f - k) * (1.f - k) * (1.f - k);
    const int shown = (int)lroundf(total * ease);
    g_ren.rect(px, totalY - 8 * s, pw - 60 * s, 1 * s, C_ACCENT, 0.5f);
    g_ren.text(px, totalY, 18 * s, isMilitaryContract(contract.type)?"Service credits":"Total", C_TEXT, 1);
    if (k >= 1.f && total > 0) g_ren.glow(x + pw - 30 * s - g_ren.textWidth(fmtMoney(total), 18 * s), totalY, g_ren.textWidth(fmtMoney(total), 18 * s), 20 * s, C_GOOD, 0.25f * clampf(1.6f - (e - 0.3f - 0.05f * rowN - 0.9f), 0.f, 1.f), 6 * s, 10 * s);
    g_ren.text(x + pw - 30 * s, totalY, 18 * s, isMilitaryContract(contract.type)?fmt("%d SC",shown):fmtMoney(shown), total >= 0 ? C_GOOD : C_BAD, 1, 2);
  }
  if (newLic) fitText(px, licY, pw - 60 * s, 22 * s, 14 * s, std::string("NEW LICENCE: ") + licenseName(career.license), C_WARN);
  { float cy = campY; for (auto& l : campaignLines) { g_ren.text(px, cy, 18 * s, l, C_ACCENT, 1); cy += 24 * s; }
    if (upNext) fitText(px, cy + 2 * s, pw - 60 * s, 15 * s, 11 * s, std::string("UP NEXT:  ") + upNext->title, C_ACCENT * 0.9f); }
  if (commitBlocked()) {   // the settlement above is what will be saved; until it is, the career stands as before the flight
    fitText(px, y + ph - 100 * s, pw - 60 * s, 20 * s, 13 * s, "NOT SAVED YET: " + saveWhy, C_BAD);
    if (button(x + 30 * s, y + ph - 66 * s, 200 * s, 46 * s, "Retry save")) retryCommit();
  }
  if (button(x + pw - 230 * s, y + ph - 66 * s, 200 * s, 46 * s, "Continue", true, true) || in.pressed[K_ENTER]) {
    retryCommit(); screen = SCR_HUB; hubTab = TAB_CONTRACTS; selContract = 0; selAircraft = -1;
    if (newLic && !commitBlocked()) { licenceShown = career.license; if (g_music.owns()) g_music.sting(STING_LICENCE); }   // (the licence's card, then the campaign's)
    if (campaignEnd && !commitBlocked()) finaleShown = true;
  }
  const bool jobWaits = !isMilitaryContract(contract.type) && career.job && career.job->state == Career::JobState::RECOVERY;
  // (a lesson failed twice running: the retry in calm air, the gusts and the turbulence mostly gone)
  const bool calmOffer = !lastSuccess && contract.type == CT_LESSON && lessonFails.count(contract.id) && lessonFails[contract.id] >= 2;
  if (calmOffer && !commitBlocked()) fitText(px, y + ph - 96 * s, pw - 60 * s, 13 * s, 10 * s, "A tough one? Try it again in calm air - with Flight assists on (Settings) it flies itself straight and level.", C_ACCENT);
  if (!lastSuccess && !commitBlocked() && button(x + 30 * s, y + ph - 66 * s, calmOffer ? 260 * s : 200 * s, 46 * s, jobWaits ? "Continue job" : calmOffer ? "Try again in calm air" : "Try again")) { if (calmOffer) calmRetry = true; retryFromDebrief(); }
}
