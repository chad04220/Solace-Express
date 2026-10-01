// Validates the hand-designed campaign: every story contract must be flyable with an aircraft the
// player can rent or afford, waypoints must clear terrain, and money pacing must not require a long grind.
#include "../src/career.h"
int main() {
  g_world.build(); buildStory();
  Career c; c.newGame();
  int problems = 0; long grind = 0;
  for (size_t i = 0; i < g_story.size(); i++) {
    const Contract& k = g_story[i];
    // terrain clearance along route legs
    std::vector<vec3> pts; pts.push_back(g_world.airports[k.from].pos());
    for (auto& w : k.wps) pts.push_back(vec3(w.x, w.alt, w.z));
    pts.push_back(g_world.airports[k.to].pos());
    for (auto& w : k.wps) { float h = g_world.height(w.x, w.z); if (w.alt < h + 60) { printf("  !! %s waypoint below terrain (%.0f vs %.0f)\n", k.id.c_str(), w.alt, h); problems++; } }
    // choose cheapest option
    int best = -1; long long bestCost = 1LL << 40; bool needBuy = false;
    for (int s = 0; s < kNumAircraft; s++) {
      Career tmp = c; std::string why;
      auto src = tmp.canFly(k, s, &why);
      long long cost;
      if (src == Career::SRC_LESSON) cost = 0;
      else if (src == Career::SRC_RENT) cost = kAircraft[s].rentFee;
      else if (src == Career::SRC_OWNED) cost = 0;
      else {
        // would buying it make it flyable?
        if (c.license < kAircraft[s].license) continue;
        tmp.fleet.push_back({s, c.location, 0, 0});
        if (tmp.canFly(k, s) != Career::SRC_OWNED) continue;
        cost = kAircraft[s].price;
      }
      if (cost < bestCost) { bestCost = cost; best = s; needBuy = src == Career::SRC_NONE; }
    }
    if (best < 0) { printf("  !! %s: NO aircraft can fly this contract (SOFTLOCK)\n", k.id.c_str()); problems++; continue; }
    if (needBuy) {
      if (c.money < kAircraft[best].price) { long sh = kAircraft[best].price - c.money; grind += sh; printf("  .. grind $%ld to buy %s\n", sh, kAircraft[best].name); c.money = kAircraft[best].price; }
      std::string m; c.buy(best, &m);
    }
    FlightResult r; r.success = true; r.touchdownFpm = 250; r.flightMin = 8;
    int stars; auto src = c.canFly(k, best);
    auto lines = c.settle(k, best, src, r, &stars);
    int net = 0; for (auto& l : lines) net += l.amount;
    printf("%-3s %-42s %-17s %6.1fkm net %7d  bank %8d  lic %d\n", k.id.c_str(), k.title.c_str(), kAircraft[best].name, g_world.distanceKm(k.from, k.to), net, c.money, c.license);
  }
  printf("Total extra freelance money needed: $%ld, problems: %d, finished=%d\n", grind, problems, c.finished);
  // Freelance board must never be empty once licensed
  for (int ap = 0; ap < (int)g_world.airports.size(); ap++) {
    Career t; t.newGame(); t.license = surfaceRough(g_world.airports[ap].surface) && g_world.airports[ap].surface != SURF_GRASS && g_world.airports[ap].surface != SURF_SAND ? LIC_CPL : LIC_PPL; t.location = ap; t.refreshBoard();
    long sum = 0; for (auto& b : t.board) sum += b.payout;
    printf("  board %s lic%d: %zu jobs avg $%ld\n", g_world.airports[ap].code, t.license, t.board.size(), t.board.empty() ? 0 : sum / (long)t.board.size());
    if (t.board.empty()) { printf("  !! no freelance jobs for PPL at %s\n", g_world.airports[ap].code); problems++; }
  }
  return problems ? 1 : 0;
}
