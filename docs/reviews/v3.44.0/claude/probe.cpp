// Analysis probe (not part of the repo): confirms suspected gameplay issues through the game's own code.
#include "game.h"
#include <cstdio>
#include <cstring>
#include <map>

static int specOf(const char* id) { for (int i = 0; i < kNumAircraft; i++) if (!strcmp(kAircraft[i].id, id)) return i; return -1; }
static int storyOf(const char* id) { for (size_t i = 0; i < g_story.size(); i++) if (g_story[i].id == id) return (int)i; return -1; }

// fly a contract from the ground: scripted take-off, then the autopilot to the destination (as tests/gameplay_test A4)
struct GameTest {
static void flyOnAutopilot(Game& g, const Contract& c, int spec, Career::Source src, float turbOverride) {
  Contract k = c;
  if (turbOverride >= 0) k.wx.turbulence = turbOverride;
  g.career.license = LIC_ATP; g.career.location = k.from; g.career.money = 500000;
  g.startFlight(k, spec, src); g.parkingBrake = false;
  const AircraftSpec& s = kAircraft[spec];
  Plane& p = g.plane; bool ap = false;
  float minPatient = 1;
  for (int i = 0; i < 30 * 60 * 40 && g.screen == SCR_FLIGHT; i++) {
    if (!ap) {
      p.ctl.brake = 0; p.ctl.throttle = 1; p.ctl.flaps = 0.15f;
      p.ctl.pitch = p.ias > s.vref * 0.95f ? clampf(0.08f * (10.f - p.pitchDeg()), -1, 1) : 0.f;
      if (p.agl() > 150.f) { p.ctl.flaps = 0; p.ctl.gearDown = false; p.apEngage(Plane::AP_NAV, k.to, g.wx); p.apComfort = true; ap = true; }
    }
    g.update(1.f / 30.f);
    minPatient = std::min(minPatient, k.type == CT_MEDEVAC ? g.result.patient : g.result.comfort);
  }
  printf("  %s in %s, turbulence %.2f: screen %d success %d, %.1f min (limit %.0f), meter %.0f%%, maxG %.2f maxBank %.0f, stars %d\n",
         k.id.c_str(), s.name, k.wx.turbulence, g.screen, g.lastSuccess, g.flightClock / 60.f, k.timeLimitMin,
         (k.type == CT_MEDEVAC ? g.result.patient : g.result.comfort) * 100.f, g.result.maxG, g.result.maxBank, g.stars);
  for (auto& l : g.payout) printf("      %-60s %8d\n", l.label.c_str(), l.amount);
}

static int run(int argc, char** argv) {
  g_world.build(); buildStory();
  g_audio.init(48000);
  const char* which = argc > 1 ? argv[1] : "all";

  if (!strcmp(which, "all") || !strcmp(which, "meter")) {
    printf("== 1. medevac / VIP meters on the autopilot (the gentle law), real game loop\n");
    Game g; g.initHeadless(); g.botControl = true;
    const Contract& p4 = g_story[storyOf("P4")];
    int bush = specOf("bush");
    flyOnAutopilot(g, p4, bush, Career::SRC_RENT, -1.f);     // as shipped
    flyOnAutopilot(g, p4, bush, Career::SRC_RENT, 0.05f);    // same mission, calm air
  }


  if (!strcmp(which, "cruise")) {
    printf("== 1b. the patient meter in straight, level autopilot cruise (P4's mission, the Bushmaster), 5 simulated minutes\n");
    for (float turb : {0.05f, 0.25f, 0.35f, 0.45f, 0.55f}) {
      Game g; g.initHeadless(); g.botControl = true;
      Contract k = g_story[storyOf("P4")]; k.wx.turbulence = turb;
      int spec = specOf("bush");
      g.career.license = LIC_ATP; g.career.location = k.from;
      g.startFlight(k, spec, Career::SRC_RENT); g.isolatedFlight = false;
      const Airport& d = g_world.airports[k.to];
      vec3 pos = d.threshold(false) - d.dir() * 40000.f; pos.y = std::max(d.elev, g_world.height(pos.x, pos.z)) + 1800.f;
      g.plane.reset(&kAircraft[spec], pos, d.heading, kAircraft[spec].maxFuel, 165, true, kAircraft[spec].cruise * 0.85f);
      g.takeoffAnnounced = true; g.engineAutoStarted = true; g.atcF.phase = 3; g.atcF.airborne = true; g.parkingBrake = false;
      g.plane.apEngage(Plane::AP_HOLD, -1, g.wx); g.plane.apComfort = true; g.plane.apHeading = d.heading; g.plane.apAlt = pos.y; g.plane.apSpeed = kAircraft[spec].cruise * 0.85f;
      float maxG = 1, minG = 1, maxBank = 0;
      for (float t = 0; t < 300.f && g.screen == SCR_FLIGHT; t += 1.f / 30.f) {
        g.update(1.f / 30.f);
        maxG = std::max(maxG, g.plane.gLoad); minG = std::min(minG, g.plane.gLoad); maxBank = std::max(maxBank, fabsf(g.plane.bankDeg()));
      }
      printf("  turbulence %.2f: patient %3.0f%% after 5 min  (g %.2f..%.2f, bank up to %.0f deg)\n", turb, g.result.patient * 100.f, minG, maxG, maxBank);
    }
  }

  if (!strcmp(which, "story")) {
    printf("== 4. every story mission x every aircraft the player could pick at that stage: the background autopilot flight\n");
    for (size_t i = 0; i < g_story.size(); i++) {
      const Contract& c = g_story[i];
      Career k; k.newGame(); k.storyIndex = (int)i; k.location = c.from; k.money = 10000000;
      // the licence the player holds when this mission is next: the highest granted by the missions before it
      k.license = LIC_STUDENT; for (size_t j = 0; j < i; j++) if (g_story[j].grantLicense > k.license) k.license = g_story[j].grantLicense;
      for (int si = 0; si < kNumAircraft; si++) {
        if (kAircraft[si].special) continue;
        k.fleet.clear(); if (c.ownedOnly) k.fleet.push_back({si, c.from, kAircraft[si].maxFuel, 1.f});
        std::string why; Career::Source src = k.canFly(c, si, &why);
        if (src == Career::SRC_NONE) continue;
        Career::LaunchPlan pl = k.plan(c, si, src);
        float fuel = -1, m = simulateFlightMinutes(c, si, &fuel);
        bool late = c.timeLimitMin > 0 && m > c.timeLimitMin;
        printf("  %-3s %-34s %-17s quote %5.1f min  autopilot %6.1f min%s  fuel %5.0f/%5.0f kg%s%s\n", c.id.c_str(), c.title.substr(0, 34).c_str(), kAircraft[si].name,
               pl.minutesEst, m, c.timeLimitMin > 0 ? (late ? " LATE" : "  ok ") : "     ", fuel, kAircraft[si].maxFuel, m < 0 ? "  <- DID NOT ARRIVE" : "",
               (c.type == CT_MEDEVAC || c.type == CT_VIP) && c.wx.turbulence > 0.25f ? "  (meter drains in this air)" : "");
      }
    }
  }

  if (!strcmp(which, "why")) {
    printf("== 4b. why the background autopilot flight did not arrive (a copy of simulateFlightMinutes that reports)\n");
    struct P { const char* id; const char* ac; } cases[] = {{"L3","kestrel"},{"C1","kestrel"},{"C1","swift_s6"},{"C5","swift_s6"},{"P1","bush"},{"P1","pelican"},{"P1","swift_s6"},{"P5","pelican"},{"P6","islander"},{"O6","pelican"},{"O7","pelican"},{"O8","pelican"},{"A2","meridian"},{"A5","meridian"}};
    for (auto& cs : cases) {
      const Contract& c = g_story[storyOf(cs.id)]; int si = specOf(cs.ac);
      const AircraftSpec& s = kAircraft[si]; const Airport& a = g_world.airports[c.from];
      Weather wx = c.wx; float h0 = a.heading; bool reverse = false;
      if (c.type != CT_LESSON || c.wps.empty()) { float hw0 = cosf((wx.windFrom - h0) * DEG), hw1 = cosf((wx.windFrom - h0 - 180.f) * DEG); reverse = hw1 > hw0; }
      vec3 start = a.threshold(reverse) + (reverse ? -a.dir() : a.dir()) * 30.f;
      Plane p; p.reset(&s, start, reverse ? h0 + 180.f : h0, s.maxFuel, (float)c.cargoKg + c.pax * 85.f + 85.f, c.startAirborne, s.cruise);
      p.apComfort = c.gentle(); p.sceneryHits = false;
      if (!c.startAirborne) { p.engineRunning = true; p.engineSpool = 0.f; }
      const float dt = 1 / 30.f; float t = 4.f; size_t wp = 0; int phase = c.startAirborne ? 1 : 0; std::string end = "timed out (60 min)";
      for (int k = 0; k < 60 * 60 * 30; k++) {
        if (phase == 0) {
          p.ctl.brake = 0; p.ctl.throttle = 1; p.ctl.gearDown = true; p.ctl.flaps = s.retract ? 0.15f : 0.1f;
          p.ctl.pitch = p.ias > s.vref * 0.95f ? clampf(0.08f * (10.f - p.pitchDeg()), -1, 1) : 0.f;
          if (!p.onGround) { float he = wrapAngle(((reverse ? h0 + 180.f : h0) - p.heading()) * DEG) / DEG; p.ctl.roll = clampf(0.05f * (clampf(he * 0.8f, -10.f, 10.f) - p.bankDeg()), -1, 1); }
          if (p.agl() > 120.f) { p.ctl.flaps = 0; if (s.retract) p.ctl.gearDown = false; phase = 1; }
        }
        if (phase == 1) {
          if (wp < c.wps.size()) { vec3 d(c.wps[wp].x - p.pos.x, 0, c.wps[wp].z - p.pos.z);
            if (p.apMode != Plane::AP_HOLD || !p.apOn) { p.apEngage(Plane::AP_HOLD, -1, wx); p.apComfort = c.gentle(); p.apSpeed = s.cruise * 0.85f; }
            p.apHeading = wrapDeg360(atan2f(d.x, -d.z) / DEG); p.apAlt = c.wps[wp].alt; p.apUseVS = false; if (length(d) < 300.f) wp++; }
          else { p.apEngage(Plane::AP_NAV, c.to, wx); p.apComfort = c.gentle(); phase = 2; }
        }
        p.step(dt, wx, t); t += dt;
        if (p.ev.crashed) { end = "CRASHED: " + p.ev.crashReason; break; }
        if (phase == 2 && p.apDone) { end = "arrived"; break; }
      }
      const Airport& B = g_world.airports[c.to];
      float dB = length(vec3(p.pos.x - B.x, 0, p.pos.z - B.z));
      printf("  %-3s %-16s t=%5.1f min phase %d  %-50s  fuel %5.1f kg  %.1f km from %s, agl %.0f m, ap %d mode %d, status '%s' decline '%s'\n", cs.id, s.name, t / 60.f, phase, end.c_str(), p.fuel, dB / 1000.f, B.code, p.agl(), p.apOn, (int)p.apMode, p.apStatus.c_str(), p.apDecline.c_str());
    }
  }

  if (!strcmp(which, "fleet")) {
    printf("%-18s %5s %8s %6s %4s %6s %6s %6s %8s %8s\n", "aircraft", "lic", "price", "rent", "pax", "cargo", "range", "kt", "used", "$/seat");
    for (int i = 0; i < kNumAircraft; i++) { const AircraftSpec& s = kAircraft[i]; if (s.special) continue;
      printf("%-18s %5d %8d %6d %4d %6.0f %6.0f %6.0f %8d %8d\n", s.name, s.license, s.price, (int)s.rentFee, s.pax, s.cargoKg, s.rangeKm, s.cruise * MS_TO_KT, s.price * 65 / 100, s.pax ? s.price / s.pax : 0); }
  }

  if (!strcmp(which, "timed")) {
    printf("== 5. timed freelance jobs (medevac and rush): can the autopilot make the deadline in the best eligible aircraft?\n");
    int n = 0, feasible = 0, noArrive = 0;
    for (uint32_t seed = 1; seed <= 60 && n < 40; seed++) for (int ap = 0; ap < (int)g_world.airports.size() && n < 40; ap += 3) {
      Career k; k.newGame(); k.license = LIC_ATP; k.boardSeed = seed; k.location = ap; k.refreshBoard();
      for (auto& c : k.board) {
        if (c.timeLimitMin <= 0 || n >= 40) continue;
        float best = 1e9f; int bestSi = -1; int tried = 0;
        for (int si = 0; si < kNumAircraft; si++) {
          if (kAircraft[si].special || k.canFly(c, si) == Career::SRC_NONE) continue;
          tried++; float m = simulateFlightMinutes(c, si);
          if (m > 0 && m < best) { best = m; bestSi = si; }
        }
        n++;
        if (bestSi < 0) noArrive++; else if (best <= c.timeLimitMin) feasible++;
        printf("  %-12s %-44s limit %4.0f  best autopilot %6.1f min (%s, %d types tried)%s\n", contractTypeName(c.type), c.title.substr(0, 44).c_str(), c.timeLimitMin,
               bestSi < 0 ? -1.f : best, bestSi < 0 ? "-" : kAircraft[bestSi].name, tried, bestSi >= 0 && best > c.timeLimitMin ? "  LATE" : bestSi < 0 ? "  none arrived" : "");
      }
    }
    printf("  %d timed jobs: %d makeable on the autopilot, %d late in every type, %d where no background flight arrived\n", n, feasible, n - feasible - noArrive, noArrive);
  }

  if (!strcmp(which, "names")) {
    std::map<std::string, int> med, types; int nb = 0;
    for (uint32_t seed = 1; seed <= 3000; seed++) for (int ap = 0; ap < (int)g_world.airports.size(); ap++) {
      Career k; k.newGame(); k.license = LIC_ATP; k.boardSeed = seed; k.location = ap; k.refreshBoard(); nb++;
      for (auto& c : k.board) { types[contractTypeName(c.type)]++; if (c.type == CT_MEDEVAC) { auto a = c.title.find(": "), b = c.title.find(" to "); med[c.title.substr(a + 2, b - a - 2)]++; } }
    }
    printf("%d boards. job types:\n", nb); for (auto& t : types) printf("  %-14s %6d\n", t.first.c_str(), t.second);
    printf("medevac patients:\n"); for (auto& m : med) printf("  %-28s %6d\n", m.first.c_str(), m.second);
  }
  if (!strcmp(which, "all") || !strcmp(which, "airline")) {
    printf("== 2. airline ticks from 'diversions' back to the departure field\n");
    Career k; k.newGame();
    k.license = LIC_ATP; k.money = 100000; k.location = g_world.findAirport("CAP");
    int q400 = specOf("meridian");
    // two Q400s on routes with two good pilots
    for (int i = 0; i < 2; i++) k.fleet.push_back({q400, k.location, kAircraft[q400].maxFuel, 1.f});
    for (int i = 0; i < 2; i++) { Career::Pilot pl; pl.name = i ? "B" : "A"; pl.rating = 3; pl.wage = 360; k.airline.pilots.push_back(pl); }
    std::string msg;
    k.assignRoute(0, g_world.findAirport("KLO"), 0, &msg); printf("  %s\n", msg.c_str());
    k.assignRoute(1, g_world.findAirport("FJH"), 1, &msg); printf("  %s\n", msg.c_str());
    // a rented Kestrel cargo job out of CAP: the first leg pays the hire, later legs pay nothing
    Contract c; c.id = "PROBE"; c.type = CT_CARGO; c.from = k.location; c.to = g_world.findAirport("MDB"); c.cargoKg = 20; c.payout = 900; c.minLicense = LIC_PPL;
    int kes = specOf("kestrel");
    Career::LaunchPlan pl = k.plan(c, kes, Career::SRC_RENT);
    k.accept(c, kes, Career::SRC_RENT, pl);
    int m0 = k.money;
    for (int leg = 0; leg < 10; leg++) {
      FlightResult r; r.landed = true; r.flightMin = 1.5f; r.outcome = OUT_DIVERTED; r.divertedTo = c.from; r.touchdownFpm = 200;
      auto L = k.closeLeg(r, k.job->plan, c.from, 0, "");
      int tot = 0; for (auto& l : L) tot += l.amount;
      if (leg < 2 || leg == 9) { printf("  leg %d (take off, land back at %s): %+d\n", leg + 1, g_world.airports[c.from].code, tot); for (auto& l : L) printf("      %-60s %8d\n", l.label.c_str(), l.amount); }
    }
    printf("  10 circuits at the departure: money %d -> %d (%+d), job still open: %d at %s\n", m0, k.money, k.money - m0, (bool)k.job, k.job ? g_world.airports[k.job->at].code : "-");
  }

  if (!strcmp(which, "all") || !strcmp(which, "vip")) {
    printf("== 3. VIP job title vs brief (two separate draws of the VIP's name)\n");
    const char* vip[] = {"A minister", "A film star", "The island's governor", "A racing driver", "An opera singer", "A football squad's captain"};
    int n = 0, bad = 0;
    for (uint32_t seed = 1; seed <= 400; seed++) for (int ap = 0; ap < (int)g_world.airports.size(); ap++) {
      Career k; k.newGame(); k.license = LIC_ATP; k.boardSeed = seed; k.location = ap; k.refreshBoard();
      for (auto& c : k.board) if (c.type == CT_VIP) {
        n++;
        const char* inTitle = nullptr; const char* inBrief = nullptr;
        for (auto v : vip) { if (c.title.find(v) != std::string::npos) inTitle = v; if (c.brief.rfind(v, 0) == 0) inBrief = v; }
        if (inTitle != inBrief) { if (bad < 3) printf("  title \"%s\"  /  brief \"%.60s...\"\n", c.title.c_str(), c.brief.c_str()); bad++; }
      }
    }
    printf("  %d of %d VIP jobs name a different VIP in the title and the brief\n", bad, n);
  }
  return 0;
}
};
int main(int argc, char** argv) { return GameTest::run(argc, argv); }
