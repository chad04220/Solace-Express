// Validates the hand-designed campaign: every story contract must be flyable with an aircraft the
// player can rent or afford, waypoints must clear terrain, and money pacing must not require a long grind.
#include "../src/career.h"
int main() {
  g_world.build(); buildStory();
  Career c; c.newGame();
  int problems = 0; long grind = 0; int loans = 0;
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
      // the money model takes the loan when it can't pay cash: a quarter down, the rest per flight (C5). Grind, where
      // it remains, is the shortfall on the down payment; it must stay within a handful of freelance jobs.
      std::string m;
      if (c.money >= kAircraft[best].price) c.buy(best, &m);
      else {
        int down = c.downPayment(best);
        if (c.money < down) { long sh = down - c.money; grind += sh; printf("  .. grind $%ld for the down payment on %s\n", sh, kAircraft[best].name); c.money = down; }
        if (!c.finance(best, &m)) { printf("  !! %s: finance(%s) refused: %s\n", k.id.c_str(), kAircraft[best].name, m.c_str()); problems++; c.money = kAircraft[best].price; c.buy(best, &m); }
        else loans++;
      }
    }
    {   // fuel and weight: the estimate with a quarter to spare fits the tanks, and the aircraft stays under its limit with it
      Career::Source src0 = c.canFly(k, best);
      Career::LaunchPlan pl = c.plan(k, best, src0 == Career::SRC_NONE ? Career::SRC_RENT : src0);
      const AircraftSpec& s = kAircraft[best];
      // (lessons fly the school aircraft with its tanks full and pay nothing: no fuel question there). The quick estimate
      // is pessimistic; where it says the tanks are too small, the quote the card really shows (the job flown on the
      // autopilot) decides. A story contract must leave at least 5% of reserve over that with full tanks.
      float est = pl.fuelKgEst;
      if (k.forceAircraft < 0 && est * 1.25f > s.maxFuel) { float flown = -1; float mins = simulateFlightMinutes(k, best, &flown); if (mins > 0 && flown > 0) est = flown; }
      float need = std::min(est * 1.25f, s.maxFuel), payload = (float)k.cargoKg + k.pax * 85.f + 85.f;
      if (k.forceAircraft < 0 && est * 1.05f > s.maxFuel) { printf("  !! %s: %s needs %.0f kg of fuel, tanks hold %.0f: no reserve\n", k.id.c_str(), s.name, est, s.maxFuel); problems++; }
      if (s.emptyMass + std::min(need, s.maxFuel) + payload > s.maxMass() + 0.5f) { printf("  !! %s: %s over the take-off weight at minimum fuel (%.0f > %.0f kg)\n", k.id.c_str(), s.name, s.emptyMass + need + payload, s.maxMass()); problems++; }
      if (src0 == Career::SRC_OWNED) c.money -= pl.fuelCostEst;   // (the money model includes the fuel bought)
    }
    FlightResult r; r.success = true; r.landed = k.id != "L1"; r.touchdownFpm = 250; r.flightMin = 8;
    int stars; auto src = c.canFly(k, best);
    auto lines = c.settle(k, best, src, r, &stars);
    int net = 0; for (auto& l : lines) net += l.amount;
    printf("%-3s %-42s %-17s %6.1fkm net %7d  bank %8d  lic %d\n", k.id.c_str(), k.title.c_str(), kAircraft[best].name, g_world.distanceKm(k.from, k.to), net, c.money, c.license);
  }
  // every story contract has a continuation policy: lessons and checkrides are retaken whole, timed jobs and the
  // VIP charter resume against their clock, the medevac resumes with its destination, the rest resume
  for (auto& k : g_story) {
    Career::JobPolicy p = Career::policyOf(k);
    bool want = p != Career::POL_UNSET
      && ((k.forceAircraft >= 0 || k.grantLicense >= 0) ? p == Career::POL_RETAKE : true)
      && (k.type == CT_MEDEVAC ? p == Career::POL_MEDEVAC : true)
      && ((k.type == CT_VIP || (k.timeLimitMin > 0 && k.forceAircraft < 0 && k.grantLicense < 0 && k.type != CT_MEDEVAC)) ? p == Career::POL_RESUME_CLOCK : true);
    if (!want) { printf("  !! %s: continuation policy %d doesn't fit the contract\n", k.id.c_str(), (int)p); problems++; }
  }
  if (Career::policyOf(g_story[0]) != Career::POL_RETAKE || Career::policyOf(g_story[3]) != Career::POL_RETAKE) { printf("  !! L1 / L4 must be retaken whole\n"); problems++; }
  // financing pacing: with the loan the whole story needs at most a few freelance jobs of grind in total (a good
  // freelance job nets about $1500 early on), and no loan may still be open with payments missed at the end
  if (grind > 5 * 1500) { printf("  !! the story needs $%ld of freelance grind even with financing (> 5 jobs)\n", grind); problems++; }
  if (c.loan.open() && c.loan.missed > 0) { printf("  !! the story ends with %d loan payments missed\n", c.loan.missed); problems++; }
  printf("Total extra freelance money needed: $%ld (%d loans taken), problems: %d, finished=%d\n", grind, loans, problems, c.finished);
  // Freelance board must never be empty once licensed
  for (int ap = 0; ap < (int)g_world.airports.size(); ap++) {
    Career t; t.newGame(); t.license = surfaceRough(g_world.airports[ap].surface) && g_world.airports[ap].surface != SURF_GRASS && g_world.airports[ap].surface != SURF_SAND ? LIC_CPL : LIC_PPL; t.location = ap; t.refreshBoard();
    long sum = 0; for (auto& b : t.board) sum += b.payout;
    printf("  board %s lic%d: %zu jobs avg $%ld\n", g_world.airports[ap].code, t.license, t.board.size(), t.board.empty() ? 0 : sum / (long)t.board.size());
    if (t.board.empty()) { printf("  !! no freelance jobs for PPL at %s\n", g_world.airports[ap].code); problems++; }
  }
  // there is always a way to earn: every airport x licence x balance x fleet state has a job the player can fly for
  // a positive net (or a free lesson next); the board is never empty
  {
    int bad = 0, checked = 0;
    const int moneys[] = {-8000, -1500, 0, 500, 3000};
    for (int ap = 0; ap < (int)g_world.airports.size(); ap++)
      for (int lic = LIC_STUDENT; lic <= LIC_ATP; lic++)
        for (int m : moneys)
          for (int fl = 0; fl < 2; fl++) {
            Career t; t.newGame(); t.license = lic; t.location = ap; t.money = m;
            t.storyIndex = lic == LIC_STUDENT ? 0 : 4;   // (a student always has a lesson next)
            if (fl) t.fleet.push_back({1, (ap + 5) % (int)g_world.airports.size(), 10.f, 0.f});   // one owned Wren, far away
            t.refreshBoard();
            checked++;
            if (!t.earningPath() || (lic >= LIC_PPL && t.board.empty())) { bad++; if (bad < 6) printf("  !! no earning path at %s lic %d money %d fleet %d (board %zu)\n", g_world.airports[ap].code, lic, m, fl, t.board.size()); }
          }
    printf("Earning path: %d states checked, %d without a way to earn\n", checked, bad);
    problems += bad;
  }
  return problems ? 1 : 0;
}
