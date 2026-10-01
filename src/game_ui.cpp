// Air Xpress - menus, hub screens and flight HUD
#include "game.h"

static const vec3 C_PANEL(0.04f, 0.07f, 0.11f), C_ACCENT(1.0f, 0.72f, 0.22f), C_TEXT(0.95f, 0.96f, 0.98f), C_DIM(0.62f, 0.68f, 0.76f);
static const vec3 C_GOOD(0.45f, 0.95f, 0.55f), C_BAD(1.0f, 0.42f, 0.35f), C_BTN(0.12f, 0.18f, 0.26f), C_BTN_HI(0.2f, 0.3f, 0.42f);

float Game::S() const { return std::max(0.6f, g_ren.H / 720.f); }

bool Game::hovered(float x, float y, float w, float h) const { return in.mx >= x && in.mx < x + w && in.my >= y && in.my < y + h; }

void Game::panel(float x, float y, float w, float h, float a) {
  g_ren.rect(x, y, w, h, C_PANEL, a, 10 * S());
  g_ren.rect(x, y, w, 2 * S(), C_ACCENT, 0.35f * a);
}

bool Game::button(float x, float y, float w, float h, const std::string& label, bool enabled, bool highlight) {
  bool hov = enabled && hovered(x, y, w, h);
  vec3 c = !enabled ? vec3(0.1f, 0.12f, 0.15f) : highlight ? C_ACCENT * 0.85f : hov ? C_BTN_HI : C_BTN;
  g_ren.rect(x, y, w, h, c, 0.92f, 6 * S());
  float ts = std::min(h * 0.5f, 20 * S());
  g_ren.text(x + w * 0.5f, y + (h - ts) * 0.5f - ts * 0.1f, ts, label, highlight ? vec3(0.05f, 0.05f, 0.08f) : enabled ? C_TEXT : C_DIM * 0.6f, 1.f, 1, !highlight);
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
    float ts = 17 * s, w = g_ren.textWidth(t.text, ts) + 30 * s;
    g_ren.rect(g_ren.W * 0.5f - w * 0.5f, y, w, ts + 14 * s, vec3(0, 0, 0), 0.45f * a, 8 * s);
    g_ren.text(g_ren.W * 0.5f, y + 6 * s, ts, t.text, t.col, a, 1);
    y += ts + 20 * s;
  }
  if (hubMsgTime > 0 && screen == SCR_HUB) {
    float ts = 18 * s, w = g_ren.textWidth(hubMsg, ts) + 40 * s;
    g_ren.rect(g_ren.W * 0.5f - w * 0.5f, g_ren.H - 70 * s, w, ts + 16 * s, C_PANEL, 0.95f, 8 * s);
    g_ren.text(g_ren.W * 0.5f, g_ren.H - 62 * s, ts, hubMsg, C_ACCENT, 1, 1);
  }
}

// ------------------------------------------------------------------ main menu
void Game::drawMenu() {
  float s = S(), W = (float)g_ren.W, H = (float)g_ren.H;
  g_ren.rect(0, 0, W * 0.42f, H, vec3(0.02f, 0.04f, 0.07f), 0.55f);
  g_ren.text(60 * s, 90 * s, 74 * s, "AIR XPRESS", C_TEXT, 1, 0);
  g_ren.text(64 * s, 172 * s, 22 * s, "Pilot career across the Solace Islands", C_ACCENT, 1, 0);
  float y = 250 * s, bw = 300 * s, bh = 52 * s;
  if (hasSave) {
    if (button(60 * s, y, bw, bh, "Continue Career", true, true)) { screen = SCR_HUB; career.refreshBoard(); }
    y += bh + 14 * s;
  }
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
  g_ren.text(60 * s, H - 70 * s, 15 * s, "F11 fullscreen   |   Gamepad supported   |   R radio, M muffle engine in flight", C_DIM, 0.9f);
  g_ren.text(60 * s, H - 45 * s, 13 * s, "v1.0  -  Real-time GPU ray-traced terrain, water, clouds and aircraft", C_DIM, 0.6f);
}

// ------------------------------------------------------------------ hub
void Game::drawHub() {
  float s = S(), W = (float)g_ren.W, H = (float)g_ren.H;
  // top bar
  g_ren.rect(0, 0, W, 64 * s, vec3(0.02f, 0.04f, 0.07f), 0.88f);
  g_ren.text(20 * s, 16 * s, 30 * s, "AIR XPRESS", C_TEXT, 1);
  const Airport& loc = g_world.airports[career.location];
  float x = 250 * s;
  g_ren.text(x, 12 * s, 14 * s, "PILOT LICENCE", C_DIM, 1); g_ren.text(x, 30 * s, 18 * s, licenseName(career.license), C_TEXT, 1);
  x += 290 * s;
  g_ren.text(x, 12 * s, 14 * s, "BANK", C_DIM, 1); g_ren.text(x, 30 * s, 20 * s, fmtMoney(career.money), career.money < 0 ? C_BAD : C_GOOD, 1);
  x += 170 * s;
  g_ren.text(x, 12 * s, 14 * s, "REPUTATION", C_DIM, 1); g_ren.text(x, 30 * s, 18 * s, fmt("%d", career.reputation), C_ACCENT, 1);
  x += 130 * s;
  g_ren.text(x, 12 * s, 14 * s, "LOCATION", C_DIM, 1); g_ren.text(x, 30 * s, 18 * s, fmt("%s  %s", loc.code, loc.name), C_TEXT, 1);
  // tabs
  const char* tabs[] = {"Contracts", "Hangar", "Logbook", "Settings"};
  float tx = 20 * s, ty = 76 * s;
  for (int i = 0; i < 4; i++) { if (button(tx, ty, 150 * s, 38 * s, tabs[i], true, hubTab == i)) hubTab = i; tx += 160 * s; }
  if (button(W - 300 * s, ty, 130 * s, 38 * s, showRadio ? "Radio <" : "Radio", true, showRadio)) showRadio = !showRadio;
  if (button(W - 160 * s, ty, 140 * s, 38 * s, "Main Menu")) { screen = SCR_MENU; saveGame(); }
  float cx = 20 * s, cy = 126 * s, cw = W - 40 * s, ch = H - 146 * s;
  switch (hubTab) {
    case TAB_CONTRACTS: drawHubContracts(cx, cy, cw, ch); break;
    case TAB_HANGAR: drawHubHangar(cx, cy, cw, ch); break;
    case TAB_LOGBOOK: drawHubLogbook(cx, cy, cw, ch); break;
    default: panel(cx, cy, std::min(cw, 760 * s), ch); drawSettings(cx + 24 * s, cy + 20 * s, std::min(cw, 760 * s) - 48 * s, ch); break;
  }
  if (showRadio) drawRadioPanel(W - 440 * s, 126 * s);
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
    vec3 c = (int)i == career.location ? vec3(0.3f, 0.8f, 1.f) : key ? C_ACCENT : vec3(1, 1, 1);
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
  g_ren.text(x + 16 * s, cy, 16 * s, career.finished ? "CAMPAIGN COMPLETE - freelance jobs continue" : "AVAILABLE WORK", C_DIM, 1); cy += 28 * s;
  for (int i = 0; i < (int)cards.size(); i++) {
    float chh = 66 * s;
    if (cy + chh > y + h - 8 * s) break;
    bool sel = i == selContract;
    bool hov = hovered(x + 10 * s, cy, lw - 20 * s, chh);
    g_ren.rect(x + 10 * s, cy, lw - 20 * s, chh, sel ? vec3(0.16f, 0.26f, 0.38f) : hov ? vec3(0.1f, 0.16f, 0.24f) : vec3(0.07f, 0.11f, 0.17f), 0.95f, 8 * s);
    if (cards[i].story) g_ren.rect(x + 10 * s, cy, 5 * s, chh, C_ACCENT, 1, 2 * s);
    if (hov && in.mPressed[0]) { selContract = i; selAircraft = -1; g_audio.trigger(SFX_CLICK); }
    if (cards[i].free) {
      g_ren.text(x + 24 * s, cy + 9 * s, 18 * s, "Free Flight / Ferry", C_TEXT, 1);
      g_ren.text(x + 24 * s, cy + 36 * s, 14 * s, "Fly anywhere for fun or to reposition. No pay.", C_DIM, 1);
    } else {
      const Contract& c = *cards[i].c;
      std::string tag = cards[i].story ? fmt("STORY CH.%d  ", c.chapter) : "";
      g_ren.text(x + 24 * s, cy + 9 * s, 17 * s, tag + c.title, cards[i].story ? C_ACCENT : C_TEXT, 1);
      std::string sub = fmt("%s  %s > %s  %.0f km", contractTypeName(c.type), g_world.airports[c.from].code, g_world.airports[c.to].code, g_world.distanceKm(c.from, c.to));
      g_ren.text(x + 24 * s, cy + 37 * s, 14 * s, sub, C_DIM, 1);
      g_ren.text(x + lw - 24 * s, cy + 37 * s, 16 * s, fmtMoney(c.payout), C_GOOD, 1, 2);
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
  g_ren.text(px, py, 26 * s, c.title, cd.story ? C_ACCENT : C_TEXT, 1); py += 40 * s;
  float mapW = std::min(iw * 0.42f, h * 0.48f);
  float textW = iw - mapW - 20 * s;
  for (auto& l : wrap(c.brief, textW, 16 * s)) { g_ren.text(px, py, 16 * s, l, C_TEXT, 0.92f); py += 22 * s; }
  py += 10 * s;
  auto row = [&](const std::string& k, const std::string& v, vec3 col = C_TEXT) { g_ren.text(px, py, 15 * s, k, C_DIM, 1); g_ren.text(px + 130 * s, py, 15 * s, v, col, 1); py += 23 * s; };
  if (cd.free) {
    if (button(px + 130 * s, py - 4 * s, 34 * s, 28 * s, "<")) { do freeDest = (freeDest + (int)g_world.airports.size() - 1) % g_world.airports.size(); while (freeDest == career.location); }
    g_ren.text(px, py, 15 * s, "Destination", C_DIM, 1);
    g_ren.text(px + 172 * s, py, 15 * s, fmt("%s %s", g_world.airports[freeDest].code, g_world.airports[freeDest].name), C_ACCENT, 1);
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
  g_ren.text(px, py, 16 * s, "CHOOSE AIRCRAFT", C_DIM, 1); py += 26 * s;
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
    g_ren.rect(rx, ry, colW, rowH, sel ? vec3(0.2f, 0.32f, 0.2f) : hov ? vec3(0.12f, 0.2f, 0.28f) : vec3(0.07f, 0.1f, 0.15f), 0.95f, 6 * s);
    if (hov && in.mPressed[0]) { selAircraft = i; g_audio.trigger(SFX_CLICK); }
    g_ren.text(rx + 10 * s, ry + 7 * s, 15 * s, kAircraft[i].name, src == Career::SRC_NONE ? C_DIM * 0.6f : C_TEXT, 1);
    std::string st;
    if (src == Career::SRC_LESSON) st = "School aircraft";
    else if (src == Career::SRC_OWNED) { int fc = career.ferryCost(c, i); st = fc ? fmt("Owned (ferry %s)", fmtMoney(fc).c_str()) : "Owned"; }
    else if (src == Career::SRC_RENT) st = fmt("Rent %s", fmtMoney(kAircraft[i].rentFee).c_str());
    else st = why;
    float sts = 12.5f * s;
    while (g_ren.textWidth(st, sts) > colW * 0.55f && st.size() > 4) st = st.substr(0, st.size() - 4) + "...";
    g_ren.text(rx + colW - 10 * s, ry + 9 * s, sts, st, src == Career::SRC_NONE ? C_BAD * 0.8f : src == Career::SRC_OWNED ? C_GOOD : C_ACCENT, 1, 2);
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
  g_ren.text(x + 16 * s, cy, 16 * s, "AIRCRAFT MARKET", C_DIM, 1); cy += 30 * s;
  for (int i = 0; i < kNumAircraft; i++) {
    float chh = 54 * s;
    bool sel = selHangar == i, hov = hovered(x + 10 * s, cy, lw - 20 * s, chh);
    g_ren.rect(x + 10 * s, cy, lw - 20 * s, chh, sel ? vec3(0.16f, 0.26f, 0.38f) : hov ? vec3(0.1f, 0.16f, 0.24f) : vec3(0.07f, 0.11f, 0.17f), 0.95f, 8 * s);
    if (hov && in.mPressed[0]) { selHangar = i; g_audio.trigger(SFX_CLICK); }
    bool owned = career.ownedIndexFor(i) >= 0;
    g_ren.text(x + 24 * s, cy + 8 * s, 17 * s, kAircraft[i].name, C_TEXT, 1);
    g_ren.text(x + 24 * s, cy + 31 * s, 13 * s, kAircraft[i].role, C_DIM, 1);
    g_ren.text(x + lw - 24 * s, cy + 18 * s, 15 * s, owned ? "OWNED" : fmtMoney(kAircraft[i].price), owned ? C_GOOD : C_ACCENT, 1, 2);
    cy += chh + 8 * s;
  }
  float dx = x + lw + 16 * s, dw = w - lw - 16 * s;
  panel(dx, y, dw, h);
  const AircraftSpec& a = kAircraft[selHangar];
  float px = dx + 24 * s, py = y + 20 * s;
  g_ren.text(px, py, 30 * s, a.name, C_TEXT, 1); py += 42 * s;
  g_ren.text(px, py, 17 * s, a.role, C_ACCENT, 1); py += 36 * s;
  auto row = [&](const std::string& k, const std::string& v) { g_ren.text(px, py, 16 * s, k, C_DIM, 1); g_ren.text(px + 200 * s, py, 16 * s, v, C_TEXT, 1); py += 26 * s; };
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
  g_ren.text(px, py, 16 * s, "YOUR FLEET", C_DIM, 1); py += 26 * s;
  if (career.fleet.empty()) g_ren.text(px, py, 15 * s, "You don't own any aircraft yet. Rentals are available everywhere.", C_DIM, 1);
  for (auto& f : career.fleet) { g_ren.text(px, py, 15 * s, fmt("%s  -  at %s", kAircraft[f.spec].name, g_world.airports[f.location].code), C_TEXT, 1); py += 22 * s; }
}

void Game::drawHubLogbook(float x, float y, float w, float h) {
  float s = S();
  float lw = std::min(w * 0.4f, 500 * s);
  panel(x, y, lw, h);
  float px = x + 24 * s, py = y + 20 * s;
  g_ren.text(px, py, 26 * s, "Pilot Logbook", C_TEXT, 1); py += 46 * s;
  auto row = [&](const std::string& k, const std::string& v) { g_ren.text(px, py, 16 * s, k, C_DIM, 1); g_ren.text(px + 220 * s, py, 16 * s, v, C_TEXT, 1); py += 27 * s; };
  row("Licence", licenseName(career.license));
  row("Flights", fmt("%d", career.flights));
  row("Successful landings", fmt("%d", career.landings));
  row("Accidents", fmt("%d", career.crashes));
  row("Flight hours", fmt("%.1f", career.hours));
  row("Softest landing", career.bestLandingFpm < 9000 ? fmt("%.0f fpm", career.bestLandingFpm) : "-");
  row("Reputation", fmt("%d", career.reputation));
  row("Fleet", fmt("%d aircraft", (int)career.fleet.size()));
  py += 20 * s;
  g_ren.text(px, py, 15 * s, "Licences unlock bigger aircraft:", C_DIM, 1); py += 24 * s;
  const char* unl[] = {"Kestrel trainer (school)", "Kestrel & Wren rentals, cargo work", "Bushmaster, Islander, Pelican; passengers; buying aircraft", "Meridian airliner & Starling jet"};
  for (int l = 0; l < 4; l++) { g_ren.text(px, py, 14 * s, fmt("%s %s", career.license >= l ? "[x]" : "[ ]", licenseName(l)), career.license >= l ? C_GOOD : C_DIM, 1); py += 19 * s; g_ren.text(px + 30 * s, py, 13 * s, unl[l], C_DIM, 0.8f); py += 22 * s; }
  float dx = x + lw + 16 * s, dw = w - lw - 16 * s;
  panel(dx, y, dw, h);
  px = dx + 24 * s; py = y + 20 * s;
  g_ren.text(px, py, 22 * s, "Story progress", C_TEXT, 1); py += 40 * s;
  const char* chapters[] = {"Flight School", "Private Pilot", "Commercial Pilot", "Owner-Operator", "Airline Captain"};
  int lastCh = -1;
  float colX = px;
  for (int i = 0; i < (int)g_story.size(); i++) {
    const Contract& c = g_story[i];
    if (c.chapter != lastCh) { if (py > y + h - 60 * s) { colX += dw * 0.5f; py = y + 60 * s; } lastCh = c.chapter; py += 6 * s; g_ren.text(colX, py, 15 * s, chapters[c.chapter], C_ACCENT, 1); py += 22 * s; }
    if (py > y + h - 30 * s) { colX += dw * 0.5f; py = y + 60 * s; }
    bool done = i < career.storyIndex, cur = i == career.storyIndex;
    g_ren.text(colX + 12 * s, py, 13.5f * s, fmt("%s %s", done ? "[x]" : cur ? " > " : "[ ]", c.title.c_str()), done ? C_GOOD : cur ? C_TEXT : C_DIM * 0.7f, 1);
    py += 19 * s;
  }
}

void Game::drawSettings(float x, float y, float w, float h) {
  float s = S();
  float py = y;
  g_ren.text(x, py, 24 * s, "Settings", C_TEXT, 1); py += 44 * s;
  auto slider = [&](const std::string& label, float& v, float lo, float hi, float step, const std::string& disp) {
    g_ren.text(x, py + 6 * s, 16 * s, label, C_DIM, 1);
    if (button(x + 250 * s, py, 36 * s, 32 * s, "-")) v = clampf(v - step, lo, hi);
    g_ren.rect(x + 296 * s, py + 12 * s, 200 * s, 8 * s, vec3(0.15f, 0.2f, 0.28f), 1, 4 * s);
    g_ren.rect(x + 296 * s, py + 12 * s, 200 * s * (v - lo) / (hi - lo), 8 * s, C_ACCENT, 1, 4 * s);
    if (hovered(x + 296 * s, py, 200 * s, 32 * s) && in.mDown[0]) v = lo + clampf((in.mx - x - 296 * s) / (200 * s), 0, 1) * (hi - lo);
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
  float w = 420 * s, h = (110 + 34 * std::min((int)stations.size(), 12)) * s;
  panel(x, y, w, h, 0.92f);
  g_ren.text(x + 18 * s, y + 14 * s, 20 * s, "Internet Radio", C_TEXT, 1);
  vec3 sc = radio.state() == Radio::PLAYING ? C_GOOD : radio.state() == Radio::FAILED ? C_BAD : C_DIM;
  std::string stat = radio.status();
  while (g_ren.textWidth(stat, 14 * s) > w - 36 * s && stat.size() > 4) stat = stat.substr(0, stat.size() - 4) + "...";
  g_ren.text(x + 18 * s, y + 44 * s, 14 * s, stat, sc, 1);
  float py = y + 72 * s;
  for (int i = 0; i < (int)stations.size() && i < 12; i++) {
    bool cur = i == set.radioStation && radio.state() != Radio::IDLE;
    if (button(x + 14 * s, py, w - 28 * s, 30 * s, stations[i].first, true, cur)) {
      set.radioStation = i; radio.setVolume(set.radioVol); radio.play(stations[i].second); saveSettings();
    }
    py += 34 * s;
  }
  if (button(x + 14 * s, py + 4 * s, 120 * s, 30 * s, "Stop")) radio.stop();
  if (button(x + 150 * s, py + 4 * s, 40 * s, 30 * s, "-")) { set.radioVol = clampf(set.radioVol - 0.1f, 0, 1); radio.setVolume(set.radioVol); }
  g_ren.text(x + 200 * s, py + 10 * s, 15 * s, fmt("Vol %.0f%%", set.radioVol * 100), C_TEXT, 1);
  if (button(x + 290 * s, py + 4 * s, 40 * s, 30 * s, "+")) { set.radioVol = clampf(set.radioVol + 0.1f, 0, 1); radio.setVolume(set.radioVol); }
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
  g_ren.rect(lx, y, tw, th, vec3(0, 0, 0), 0.5f, 6 * s);
  g_ren.rect(rx, y, tw + 10 * s, th, vec3(0, 0, 0), 0.5f, 6 * s);
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
  g_ren.rect(lx - 4 * s, cy - 15 * s, tw + 8 * s, 30 * s, vec3(0, 0, 0), 0.95f, 4 * s);
  g_ren.text(lx + tw - 6 * s, cy - 11 * s, 20 * s, fmt("%.0f", spdU), vec3(1, 1, 1), 1, 2, false);
  g_ren.rect(rx - 4 * s, cy - 15 * s, tw + 18 * s, 30 * s, vec3(0, 0, 0), 0.95f, 4 * s);
  g_ren.text(rx + 6 * s, cy - 11 * s, 20 * s, fmt("%.0f", altU), vec3(1, 1, 1), 1, 0, false);
  g_ren.text(lx + tw * 0.5f, y - 20 * s, 13 * s, set.metric ? "KM/H" : "KT", C_DIM, 1, 1);
  g_ren.text(rx + tw * 0.5f, y - 20 * s, 13 * s, set.metric ? "M" : "FT", C_DIM, 1, 1);
  // vertical speed
  float vsU = set.metric ? plane.vel.y : plane.vel.y * 196.85f;
  g_ren.text(rx + tw * 0.5f + 5 * s, y + th + 6 * s, 14 * s, set.metric ? fmt("VS %+.1f m/s", vsU) : fmt("VS %+.0f", vsU), fabsf(plane.vel.y) > 6 ? C_ACCENT : C_TEXT, 1, 1);
  // heading
  float hdg = plane.heading();
  g_ren.rect(cx - 40 * s, y + sz + 6 * s, 80 * s, 26 * s, vec3(0, 0, 0), 0.8f, 4 * s);
  g_ren.text(cx, y + sz + 10 * s, 17 * s, fmt("%03.0f", hdg), vec3(1, 1, 1), 1, 1, false);
  // AGL
  float agl = plane.agl();
  if (agl < 750) g_ren.text(lx + tw * 0.5f, y + th + 6 * s, 14 * s, fmt("RA %s", fmtAlt(agl).c_str()), agl < 60 ? C_ACCENT : C_TEXT, 1, 1);
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
  if (!hudOn) { drawToasts(); return; }
  const AircraftSpec& spc = *plane.spec;
  // mission bar
  const Airport& d = dest();
  vec3 target = wpIndex < (int)contract.wps.size() ? vec3(contract.wps[wpIndex].x, contract.wps[wpIndex].alt, contract.wps[wpIndex].z) : d.pos();
  vec3 to = target - plane.pos;
  float dist = length(vec3(to.x, 0, to.z));
  float brg = wrapDeg360(atan2f(to.x, -to.z) / DEG);
  float gs = length(vec3(plane.vel.x, 0, plane.vel.z));
  g_ren.rect(W * 0.5f - 300 * s, 8 * s, 600 * s, 54 * s, vec3(0, 0, 0), 0.45f, 10 * s);
  std::string obj = wpIndex < (int)contract.wps.size() ? fmt("Checkpoint %d/%d", wpIndex + 1, (int)contract.wps.size()) : fmt("Land at %s (%s)", d.code, d.name);
  g_ren.text(W * 0.5f, 13 * s, 16 * s, contract.title + "  -  " + obj, C_TEXT, 1, 1);
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
  g_ren.rect(ex, ey, 270 * s, 210 * s, vec3(0, 0, 0), 0.5f, 10 * s);
  float ty = ey + 12 * s;
  auto erow = [&](const std::string& k, const std::string& v, vec3 c = C_TEXT) { g_ren.text(ex + 14 * s, ty, 15 * s, k, C_DIM, 1); g_ren.text(ex + 256 * s, ty, 15 * s, v, c, 1, 2); ty += 22 * s; };
  const AircraftSpec& sp = *plane.spec;
  // throttle bar
  g_ren.text(ex + 14 * s, ty, 15 * s, "THR", C_DIM, 1);
  g_ren.rect(ex + 70 * s, ty + 3 * s, 140 * s, 12 * s, vec3(0.2f, 0.2f, 0.2f), 1, 6 * s);
  g_ren.rect(ex + 70 * s, ty + 3 * s, 140 * s * plane.ctl.throttle, 12 * s, C_ACCENT, 1, 6 * s);
  g_ren.text(ex + 256 * s, ty, 15 * s, fmt("%.0f%%", plane.ctl.throttle * 100), C_TEXT, 1, 2); ty += 22 * s;
  if (sp.engineType == ENG_PISTON) erow("RPM", plane.engineRunning ? fmt("%.0f", plane.rpm) : (plane.starterTime > 0 ? "CRANKING" : "OFF"), plane.engineRunning ? C_TEXT : C_BAD);
  else erow("N1", plane.engineRunning ? fmt("%.1f%%", plane.n1) : (plane.starterTime > 0 ? fmt("START %.0f%%", plane.n1) : "OFF"), plane.engineRunning ? C_TEXT : C_BAD);
  float fuelFrac = plane.fuel / sp.maxFuel;
  erow("FUEL", fmt("%.0f%%  ~%.0f km", fuelFrac * 100, plane.rangeLeftKm()), fuelFrac < 0.15f ? C_BAD : C_TEXT);
  erow("FLAPS", fmt("%.0f%%", plane.flaps * 100));
  std::string gearS = !sp.retract ? "FIXED" : plane.gear > 0.99f ? "DOWN" : plane.gear < 0.01f ? "UP" : "TRANSIT";
  erow("GEAR", gearS, plane.gear > 0.99f ? C_GOOD : plane.gear < 0.01f ? C_DIM : C_ACCENT);
  erow("TRIM", fmt("%+.0f", plane.ctl.trim * 100));
  erow("GND SPD", fmtSpeed(gs));
  std::string st;
  if (plane.ctl.brake > 0.5f) st += "BRAKE ";
  if (plane.apOn) st += fmt("AP %03.0f/%s ", plane.apHeading, fmtAlt(plane.apAlt).c_str());
  if (landingLight) st += "LDG LT";
  g_ren.text(ex + 14 * s, ty, 13 * s, st, C_ACCENT, 1);
  } else {
    // cockpit view: the 3D panel carries the instruments; add a compact readout strip
    std::string ro = fmt("IAS %s   ALT %s   HDG %03.0f   VS %+.0f   THR %.0f%%   FLAPS %.0f%%   %s   FUEL %.0f%%", fmtSpeed(plane.ias).c_str(), fmtAlt(plane.pos.y).c_str(),
                         plane.heading(), plane.vel.y * 196.85f, plane.ctl.throttle * 100, plane.flaps * 100,
                         !plane.spec->retract ? "GEAR FIXED" : plane.gear > 0.99f ? "GEAR DOWN" : plane.gear < 0.01f ? "GEAR UP" : "GEAR TRANSIT",
                         plane.fuel / plane.spec->maxFuel * 100);
    if (plane.apOn) ro += "   AP";
    if (plane.ctl.brake > 0.5f) ro += "   BRAKE";
    float tw = g_ren.textWidth(ro, 15 * s) + 30 * s;
    g_ren.rect(W * 0.5f - tw * 0.5f, H - 44 * s, tw, 30 * s, vec3(0, 0, 0), 0.5f, 8 * s);
    g_ren.text(W * 0.5f, H - 38 * s, 15 * s, ro, C_TEXT, 0.95f, 1);
  }
  // minimap
  float mm = 210 * s;
  float range = clampf(dist * 1.3f, 3000.f, 20000.f);
  drawMinimap(W - mm - 30 * s, 80 * s, mm, range);
  // warnings
  bool flash = fmodf(realTime, 0.8f) < 0.5f;
  float wy = H * 0.3f;
  if (plane.stallWarn > 0.8f && !plane.onGround && flash) { g_ren.text(W * 0.5f, wy, 44 * s, "STALL", C_BAD, 1, 1); wy += 52 * s; }
  bool nearDest = length(plane.pos - d.pos()) < 4000.f;
  if (!plane.onGround && plane.agl() < 120 && plane.vel.y < -7.f && flash) { g_ren.text(W * 0.5f, wy, 40 * s, "PULL UP", C_BAD, 1, 1); wy += 48 * s; }
  if (spc.retract && plane.gear < 0.99f && !plane.onGround && plane.agl() < 200 && nearDest && plane.ias < spc.vref * 1.5f && flash) { g_ren.text(W * 0.5f, wy, 36 * s, "GEAR!", C_ACCENT, 1, 1); wy += 44 * s; }
  if (!plane.engineRunning && engineAutoStarted && plane.starterTime <= 0 && flash) g_ren.text(W * 0.5f, wy, 26 * s, "ENGINE OFF - press I to restart", C_BAD, 1, 1);
  // instructor hint
  if (set.showHints && !hint.empty() && !crashed) {
    float hw = std::min(760 * s, W - 40 * s);
    auto lines = wrap(hint, hw - 40 * s, 17 * s);
    float hh = 38 * s + lines.size() * 23 * s;
    float hx = W * 0.5f - hw * 0.5f, hy = camMode == 1 ? 160 * s : H - hh - 300 * s * 0 - 30 * s;
    if (camMode != 1) hy = H - hh - 20 * s, hx = std::max(hx, 360 * s);
    if (hx + hw > W - 320 * s && camMode != 1) hw = std::max(300 * s, W - 320 * s - hx);
    g_ren.rect(hx, hy, hw, hh, vec3(0.05f, 0.08f, 0.04f), 0.8f, 10 * s);
    g_ren.rect(hx, hy, 5 * s, hh, C_GOOD, 1, 2 * s);
    g_ren.text(hx + 18 * s, hy + 8 * s, 13 * s, "INSTRUCTOR", C_GOOD, 1);
    float ly = hy + 28 * s;
    for (auto& l : wrap(hint, hw - 40 * s, 17 * s)) { g_ren.text(hx + 18 * s, ly, 17 * s, l, C_TEXT, 1); ly += 23 * s; }
  }
  // controls reminder
  if (flightClock < 25.f && !paused) g_ren.text(W * 0.5f, H - 22 * s, 13 * s, "W/S pitch  A/D roll  Q/E rudder  SHIFT/CTRL throttle  F/V flaps  G gear  B brake  Z autopilot  C camera  M muffle  R radio  N map  ESC pause", C_DIM, clampf((25.f - flightClock) / 5.f, 0, 1), 1);
  if (showRadio) drawRadioPanel(20 * s, 60 * s);
  else if (radio.state() == Radio::PLAYING) g_ren.text(20 * s, 40 * s, 13 * s, "Radio: " + stations[std::clamp(set.radioStation, 0, (int)stations.size() - 1)].first, C_DIM, 0.8f);
}

void Game::drawMapOverlay() {
  float s = S(), W = (float)g_ren.W, H = (float)g_ren.H;
  float sz = std::min(W, H) - 120 * s;
  float x = W * 0.5f - sz * 0.5f, y = H * 0.5f - sz * 0.5f + 20 * s;
  g_ren.rect(0, 0, W, H, vec3(0, 0, 0), 0.5f);
  drawMapView(x, y, sz, sz, contract.from, contract.to, &contract.wps);
  vec2 p(x + (plane.pos.x + WORLD_HALF) / (2 * WORLD_HALF) * sz, y + (plane.pos.z + WORLD_HALF) / (2 * WORLD_HALF) * sz);
  float h = plane.heading() * DEG; vec2 f(sinf(h), -cosf(h));
  g_ren.line(p.x - f.x * 10 * s, p.y - f.y * 10 * s, p.x + f.x * 14 * s, p.y + f.y * 14 * s, 5 * s, vec3(1, 1, 0.2f), 1);
  g_ren.rect(p.x - 5 * s, p.y - 5 * s, 10 * s, 10 * s, vec3(1, 1, 0.2f), 1, 5 * s);
  g_ren.text(W * 0.5f, y - 34 * s, 20 * s, "Solace Islands  -  N / TAB to close", C_TEXT, 1, 1);
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
  float pw = 760 * s, ph = 470 * s, x = W * 0.5f - pw * 0.5f, y = H * 0.5f - ph * 0.5f;
  panel(x, y, pw, ph, 0.95f);
  g_ren.text(x + 30 * s, y + 24 * s, 28 * s, "Paused", C_TEXT, 1);
  float by = y + 80 * s, bw = 240 * s, bh = 46 * s;
  if (button(x + 30 * s, by, bw, bh, "Resume", true, true)) paused = false;
  by += bh + 12 * s;
  if (button(x + 30 * s, by, bw, bh, "Restart flight")) { Contract c = contract; startFlight(c, specIdx, source); }
  by += bh + 12 * s;
  if (button(x + 30 * s, by, bw, bh, "Settings")) settingsFromPause = true;
  by += bh + 12 * s;
  if (button(x + 30 * s, by, bw, bh, showRadio ? "Hide radio" : "Radio")) showRadio = !showRadio;
  by += bh + 12 * s;
  if (button(x + 30 * s, by, bw, bh, "Abandon flight")) endFlight(false, "Abandoned flight");
  float cx = x + 310 * s, cy = y + 80 * s;
  const char* lines[] = {"W / S ........ pitch down / up", "A / D ........ roll", "Q / E ........ rudder / nosewheel", "SHIFT / CTRL . throttle (1-9, 0)",
                         "F / V ........ flaps down / up", "G ............ landing gear", "B ............ parking brake", "SPACE ........ wheel brakes",
                         "[ / ] ........ elevator trim", "Z ............ autopilot (A/D steer)", "T ............ time acceleration", "C ............ camera  (right-drag look)",
                         "L ............ landing lights", "M ............ muffle engine noise", "R ............ internet radio", "N / TAB ...... map    H ... HUD"};
  for (auto l : lines) { g_ren.text(cx, cy, 14.5f * s, l, C_DIM, 1); cy += 22 * s; }
  if (showRadio) drawRadioPanel(20 * s, 60 * s);
}

void Game::drawDebrief() {
  float s = S(), W = (float)g_ren.W, H = (float)g_ren.H;
  g_ren.rect(0, 0, W, H, vec3(0, 0, 0), 0.35f);
  float pw = std::min(720 * s, W - 40 * s), ph = std::min(600 * s, H - 40 * s), x = W * 0.5f - pw * 0.5f, y = H * 0.5f - ph * 0.5f;
  panel(x, y, pw, ph, 0.95f);
  float px = x + 30 * s, py = y + 24 * s;
  g_ren.text(px, py, 30 * s, debriefTitle, lastSuccess ? C_GOOD : C_BAD, 1); py += 44 * s;
  g_ren.text(px, py, 18 * s, contract.title, C_TEXT, 1); py += 34 * s;
  if (lastSuccess && contract.type != CT_FERRY) {
    for (int i = 0; i < 3; i++) g_ren.rect(px + i * 40 * s, py, 32 * s, 32 * s, i < stars ? C_ACCENT : vec3(0.2f, 0.22f, 0.25f), 1, 16 * s);
    py += 48 * s;
  }
  auto row = [&](const std::string& k, const std::string& v) { g_ren.text(px, py, 16 * s, k, C_DIM, 1); g_ren.text(px + 230 * s, py, 16 * s, v, C_TEXT, 1); py += 24 * s; };
  if (touchedDown) row("Touchdown", fmt("%.0f fpm", touchdownFpm));
  row("Flight time", fmt("%d:%02d", (int)flightClock / 60, (int)flightClock % 60));
  row("Max G", fmt("%.2f", plane.maxG));
  row("Max bank", fmt("%.0f deg", result.maxBank));
  row("Fuel used", fmt("%.0f kg", result.fuelUsedKg));
  py += 12 * s;
  int total = 0;
  for (auto& l : payout) {
    g_ren.text(px, py, 16 * s, l.label, C_TEXT, 1);
    g_ren.text(x + pw - 30 * s, py, 16 * s, fmtMoney(l.amount), l.amount >= 0 ? C_GOOD : C_BAD, 1, 2);
    total += l.amount; py += 24 * s;
  }
  g_ren.rect(px, py + 2 * s, pw - 60 * s, 2 * s, C_DIM, 0.6f); py += 10 * s;
  g_ren.text(px, py, 18 * s, "Total", C_TEXT, 1);
  g_ren.text(x + pw - 30 * s, py, 18 * s, fmtMoney(total), total >= 0 ? C_GOOD : C_BAD, 1, 2); py += 34 * s;
  if (career.license > licenseBefore) { g_ren.text(px, py, 22 * s, std::string("NEW LICENCE: ") + licenseName(career.license), C_ACCENT, 1); py += 34 * s; }
  if (career.finished && lastSuccess && contract.story && contract.id == g_story.back().id) { g_ren.text(px, py, 18 * s, "You've completed the Air Xpress campaign. Congratulations, Captain!", C_ACCENT, 1); py += 28 * s; }
  if (button(x + pw - 230 * s, y + ph - 66 * s, 200 * s, 46 * s, "Continue", true, true) || in.pressed[K_ENTER]) { screen = SCR_HUB; hubTab = TAB_CONTRACTS; selContract = 0; selAircraft = -1; }
  if (!lastSuccess && button(x + 30 * s, y + ph - 66 * s, 200 * s, 46 * s, "Try again")) { Contract c = contract; startFlight(c, specIdx, career.canFly(c, specIdx) != Career::SRC_NONE ? career.canFly(c, specIdx) : source); }
}
