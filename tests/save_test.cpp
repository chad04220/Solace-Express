// Career save robustness: damaged or incomplete saves are rejected without touching the live career, a failed write
// reports false and leaves the previous save intact, and a good save round-trips.
#include "../src/career.h"
#include <cmath>
#include <cstdio>
#include <string>
#ifdef _WIN32
#include <direct.h>
#define rmdir _rmdir
#else
#include <sys/stat.h>
#include <unistd.h>
#endif
static int fails = 0;
static void check(bool c, const char* what) { if (!c) { printf("  !! %s\n", what); fails++; } }
static void writeFile(const std::string& p, const char* text) { FILE* f = fopen(p.c_str(), "w"); fputs(text, f); fclose(f); }
static std::string readFile(const std::string& p) { std::string s; FILE* f = fopen(p.c_str(), "r"); if (!f) return s; char b[512]; size_t n; while ((n = fread(b, 1, sizeof b, f)) > 0) s.append(b, n); fclose(f); return s; }
int main() {
  g_world.build(); buildStory();
  const std::string p = "save_test_career.sav";
  Career live; live.newGame(); live.money = -1234; live.storyIndex = 5; live.license = LIC_PPL; live.fleet.push_back({1, 2, 50.f, 0.f});
  check(live.save(p), "good save writes");
  Career r; r.newGame();
  check(r.load(p), "good save loads");
  check(r.money == -1234 && r.storyIndex == 5 && r.license == LIC_PPL && r.fleet.size() == 1 && r.fleet[0].location == 2, "round trip keeps the values (including a negative balance)");
  {   // version 3: the attempt counter and the open-attempt marker round-trip; a version 2 file (no marker) still loads
    Career a = live; a.attempt = 17; a.attemptOpen = true;
    check(a.save("save_test_v3.sav"), "v3 save writes");
    Career b; b.newGame(); check(b.load("save_test_v3.sav") && b.attempt == 17 && b.attemptOpen, "v3 round trip keeps the attempt marker");
    writeFile("save_test_v2.sav", "solace_save 2\nmoney 900\nlicense 1\nrep 3\nlocation 1\nstory 4\nflights 2\nlandings 2\ncrashes 0\nhours 1.5\nbest 200\nseed 9\nfinished 0\nfleet 1\nplane kestrel 1 20\nend\n");
    Career v2; v2.newGame(); check(v2.load("save_test_v2.sav") && v2.money == 900 && v2.attempt == 0 && !v2.attemptOpen, "v2 save loads with the marker closed");
    writeFile("save_test_bad.sav", "solace_save 3\nmoney 900\nlicense 1\nrep 3\nlocation 1\nstory 4\nflights 2\nlandings 2\ncrashes 0\nhours 1.5\nbest 200\nseed 9\nfinished 0\nattempt 3\nattempt_open 7\nfleet 0\nend\n");
    Career x = r; check(!x.load("save_test_bad.sav"), "a marker that isn't 0 or 1 is rejected");
    remove("save_test_v3.sav"); remove("save_test_v3.sav.bak"); remove("save_test_v2.sav");
  }
  {   // version 3: a loan and the aircraft's condition round-trip; a plane line without the condition (v2) reads as new
    Career a = live; a.money = 100000; a.license = LIC_CPL; std::string m;
    check(a.finance(3, &m) && a.loan.open() && a.loan.spec == 3 && a.fleet.size() == 2, "finance opens a loan and adds the aircraft");
    check(!a.finance(4, &m), "only one loan at a time");
    a.fleet[0].condition = 0.4f;
    check(a.save("save_test_loan.sav"), "loan save writes");
    Career b; b.newGame(); check(b.load("save_test_loan.sav"), "loan save loads");
    check(b.loan.open() && b.loan.spec == 3 && b.loan.balance == a.loan.balance && b.loan.payment == a.loan.payment && b.loan.missed == 0 && std::fabs(b.loan.rate - a.loan.rate) < 1e-4f, "round trip keeps the loan");
    check(b.fleet.size() == 2 && std::fabs(b.fleet[0].condition - 0.4f) < 1e-4f && std::fabs(b.fleet[1].condition - 1.f) < 1e-4f, "round trip keeps the condition");
    {   // a job whose contract carries a forecast keeps it (wx2)
      Career j = live; j.license = LIC_CPL; j.boardSeed = 77; j.refreshBoard();
      Contract fc = j.board.empty() ? Contract() : j.board[0]; fc.id = "F77_0"; fc.wxShift = true; fc.wxEnd = fc.wx; fc.wxEnd.windFrom = 123.f; fc.wxEnd.precip = 2;
      Career::LaunchPlan pl = j.plan(fc, 1, Career::SRC_RENT); j.accept(fc, 1, Career::SRC_RENT, pl);
      check(j.save("save_test_wx2.sav"), "forecast save writes");
      Career k2; k2.newGame(); check(k2.load("save_test_wx2.sav") && k2.job && k2.job->c.wxShift && std::fabs(k2.job->c.wxEnd.windFrom - 123.f) < 1e-3f && k2.job->c.wxEnd.precip == 2, "round trip keeps the forecast");
      remove("save_test_wx2.sav"); remove("save_test_wx2.sav.bak");
    }
    {   // the airline: pilots, routes and the totals round-trip; a route without its aircraft is rejected
      Career al = live; al.license = LIC_ATP; al.fleet.push_back({5, 2, 1000.f, 0.9f}); std::string m;
      Career::Pilot p; p.name = "A. Okafor"; p.rating = 2; p.wage = 220; al.hirePilot(p, &m); check(al.assignRoute(1, 3, 0, &m), m.c_str()); al.airline.earned = 1234; al.airline.incidents = 2;
      check(al.save("save_test_air.sav"), "airline save writes");
      Career b2; b2.newGame(); check(b2.load("save_test_air.sav") && b2.airline.pilots.size() == 1 && b2.airline.pilots[0].name == "A. Okafor" && b2.airline.pilots[0].rating == 2 && b2.airline.routes.size() == 1 && b2.airline.routes[0].fleetIdx == 1 && b2.airline.routes[0].to == 3 && b2.airline.earned == 1234 && b2.airline.incidents == 2, "round trip keeps the airline");
      writeFile("save_test_air_bad.sav", "solace_save 3\nmoney 900\nlicense 3\nrep 3\nlocation 1\nstory 4\nflights 2\nlandings 2\ncrashes 0\nhours 1.5\nbest 200\nseed 9\nfinished 0\nattempt 3\nattempt_open 0\nfleet 0\nroute 0 1 2 0 0 0\nend\n");
      Career x2 = r; check(!x2.load("save_test_air_bad.sav"), "a route without its aircraft is rejected");
      remove("save_test_air.sav"); remove("save_test_air.sav.bak"); remove("save_test_air_bad.sav");
    }
    { Career i2 = a; i2.insured = true; check(i2.save("save_test_ins.sav"), "insured save writes"); Career j; j.newGame(); check(j.load("save_test_ins.sav") && j.insured && !b.insured, "round trip keeps the insurance"); remove("save_test_ins.sav"); remove("save_test_ins.sav.bak"); }
    writeFile("save_test_v2b.sav", "solace_save 2\nmoney 900\nlicense 1\nrep 3\nlocation 1\nstory 4\nflights 2\nlandings 2\ncrashes 0\nhours 1.5\nbest 200\nseed 9\nfinished 0\nfleet 1\nplane kestrel 1 20\nend\n");
    Career v2; v2.newGame(); check(v2.load("save_test_v2b.sav") && v2.fleet.size() == 1 && v2.fleet[0].condition > 0.99f && !v2.loan.open(), "v2 plane line reads as a new aircraft without a loan");
    {   // a payment comes off each settlement; three missed payments repossess the aircraft
      Career d = b; std::vector<PayoutLine> L; int bal = d.loan.balance; d.money = 1000000;
      d.payLoan(L); check(d.loan.balance == bal - d.loan.payment && !L.empty() && L.back().amount == -d.loan.payment, "a settlement takes one payment");
      d.money = -1000000; for (int i = 0; i < 3; i++) { L.clear(); d.payLoan(L); }
      check(!d.loan.open() && d.fleet.size() == 1, "three missed payments repossess the aircraft and close the loan");
      // QA C1: the airline's routes follow the fleet through a repossession (the lost aircraft's route closes, the
      // others' indices move down) and the save after it still loads
      Career e = b; e.license = LIC_ATP; e.money = -1000000;
      int fin = e.ownedIndexFor(e.loan.spec), other = fin == 0 ? 1 : 0;
      e.airline.pilots.push_back({"T. Pilot", 2, 400});
      e.airline.routes.push_back({other, e.fleet[other].location, 3, 0, 0, 0});
      for (int i = 0; i < 3; i++) { L.clear(); e.payLoan(L); }
      bool routeFollows = e.fleet.size() == 1 && e.airline.routes.size() == 1 && e.airline.routes[0].fleetIdx == 0;
      check(routeFollows, "a repossession moves the other aircraft's route down with it");
      check(e.save("save_test_repo.sav"), "the save after a repossession writes");
      Career e2; e2.newGame(); check(e2.load("save_test_repo.sav") && e2.airline.routes.size() == 1 && e2.airline.routes[0].fleetIdx == 0, "...and loads");
      remove("save_test_repo.sav"); remove("save_test_repo.sav.bak");
      Career f2 = b; f2.license = LIC_ATP; f2.money = -1000000; int fin2 = f2.ownedIndexFor(f2.loan.spec);
      f2.airline.pilots.push_back({"T. Pilot", 2, 400}); f2.airline.routes.push_back({fin2, f2.fleet[fin2].location, 3, 0, 0, 0});
      for (int i = 0; i < 3; i++) { L.clear(); f2.payLoan(L); }
      check(f2.fleet.size() == 1 && f2.airline.routes.empty(), "a repossessed aircraft's own route closes");
    }
    remove("save_test_loan.sav"); remove("save_test_loan.sav.bak"); remove("save_test_v2b.sav");
  }
  {   // QA S1: a committed freelance job of every type round-trips (the night, IFR and survey types were rejected on load)
    for (int ty = CT_LESSON; ty < CT_COUNT; ty++) {
      if (ty == CT_TRIAL) continue;
      Career j; j.newGame(); j.license = LIC_ATP; j.money = 50000;
      Career::JobState J; J.c.id = fmt("job_%d", ty); J.c.type = (ContractType)ty; J.c.from = 0; J.c.to = 1; J.c.payout = 900; J.c.timeLimitMin = 90; J.c.story = false;
      J.spec = 1; J.src = Career::SRC_RENT; J.state = Career::JobState::ACTIVE; J.at = 0; J.id = 7;
      j.job = J;
      std::string p = fmt("save_test_job%d.sav", ty);
      bool ok = j.save(p); Career r2; r2.newGame(); ok = ok && r2.load(p) && r2.job && r2.job->c.type == ty && r2.job->c.id == J.c.id;
      check(ok, fmt("a job of type %d saves and loads", ty).c_str());
      remove(p.c_str()); remove((p + ".bak").c_str());
    }
  }

  // each of these must be rejected and leave the career as it was
  const char* bad[] = {
    "solace_save 1\nlicense 1\nplane kestrel 999 70\n",                                   // fleet airport out of range
    "solace_save 1\n",                                                                    // header only
    "solace_save 5\nmoney 5\nlicense 0\nlocation 0\nstory 0\n",                           // unknown version
    "solace_save 1\nmoney 5\nlicense 9\nlocation 0\nstory 0\n",                           // bad license
    "solace_save 1\nmoney 5\nlicense 0\nlocation -3\nstory 0\n",                          // bad location
    "solace_save 1\nmoney 5\nlicense 0\nlocation 0\nstory 999\n",                         // story past the end
    "solace_save 1\nmoney 5\nlicense 0\nlocation 0\nstory 0\nhours nan\n",                // non-finite
    "solace_save 1\nmoney 5\nlicense 0\nlocation 0\nstory 0\nplane nosuchplane 0 10\n",   // unknown aircraft
    "solace_save 1\nmoney 5\nlicense 0\nlocation 0\nstory 0\nplane kestrel 0\n",          // truncated line
    "solace_save 1\nmoney 5\nlicense 0\nlocation\n",                                      // truncated value
    "garbage",
    // malformed states (the review of v3.44.0, CAR-11)
    "solace_save 1\nmoney 5\nlicense 0\nlocation 0\nstory 0\nfinished 999\n",                                   // a marker past 0 / 1
    "solace_save 1\nmoney 5\nlicense 0\nrep -4\nlocation 0\nstory 0\n",                                         // negative reputation
    "solace_save 1\nmoney 5\nlicense 3\nlocation 0\nstory 0\nplane kestrel 0 10\nplane kestrel 0 10\n",         // two of one type
    "solace_save 1\nmoney 5\nlicense 3\nlocation 0\nstory 0\nplane kestrel 0 10\nloan kestrel 5000 0 0 0.05\n", // an open loan with no payment
    "solace_save 1\nmoney 5\nlicense 3\nlocation 0\nstory 0\nplane kestrel 0 10\nplane wren 0 10\npilot Ann 1 100\npilot Bo 1 100\nroute 0 0 1 0 0 0\nroute 0 1 2 1 0 0\n",   // one aircraft on two routes
  };
  int i = 0;
  for (const char* b : bad) {
    writeFile("save_test_bad.sav", b);
    Career c = r;
    bool ok = c.load("save_test_bad.sav");
    char msg[64]; snprintf(msg, sizeof msg, "damaged save %d rejected", i++);
    check(!ok && c.money == r.money && c.storyIndex == r.storyIndex && c.fleet.size() == r.fleet.size(), msg);
  }
  // old saves without the end marker still load
  writeFile("save_test_old.sav", "airxpress_save 1\nmoney 900\nlicense 1\nrep 3\nlocation 1\nstory 4\nplane kestrel 1 20\n");
  { Career c; c.newGame(); check(c.load("save_test_old.sav") && c.money == 900 && c.fleet.size() == 1, "legacy save loads"); }

  // a failed write keeps the previous save
  std::string before = readFile(p);
  Career big = r; big.money = 42;
  // (not /dev/full: an atomic writer would rename its temp file over the device node)
  std::string blocked = p + ".tmp";
#ifdef _WIN32
  _mkdir(blocked.c_str());
#else
  mkdir(blocked.c_str(), 0755);
#endif
  check(!big.save(p), "write that can't create its temp file reports failure");
  rmdir(blocked.c_str());
  check(!big.save("no_such_dir/x/career.sav"), "write to a missing folder reports failure");
  check(readFile(p) == before, "previous save untouched");
  // a second save keeps the first as .bak
  check(big.save(p), "second save");
  check(readFile(p + ".bak") == before, "previous save kept as .bak");
  // every truncated prefix of a current save (cut at any line) is rejected, so the backup is used instead
  {
    Career t = r; t.fleet.push_back({2, 1, 30.f, 0.f}); t.flights = 31; t.hours = 16.5f;
    check(t.save("save_test_trunc.sav"), "truncation source writes");
    std::string full = readFile("save_test_trunc.sav");
    int accepted = 0;
    for (size_t k = 0; k + 1 < full.size(); k++) if (full[k] == '\n') {
      writeFile("save_test_bad.sav", full.substr(0, k + 1).c_str());
      Career c = r; if (c.load("save_test_bad.sav")) accepted++;
    }
    check(accepted == 0, "no truncated current save loads");
    Career c; c.newGame(); check(c.load("save_test_trunc.sav") && c.fleet.size() == 2 && c.flights == 31, "the complete save loads");
    remove("save_test_trunc.sav"); remove("save_test_trunc.sav.bak");
  }
  // recovered from the backup with a damaged primary: saving again never rotates the damaged file over the backup
  {
    std::string good = readFile(p);
    writeFile(p, "solace_save 2\nmoney 5\n");   // damaged primary
    Career c; c.newGame();
    check(!c.load(p) && c.load(p + ".bak"), "damaged primary, good backup");
    check(c.save(p), "save after recovery");
    Career b; b.newGame(); check(b.load(p + ".bak"), "backup still loads after saving over a damaged primary");
    Career m; m.newGame(); check(m.load(p), "new primary loads");
    (void)good;
  }
  remove(p.c_str()); remove((p + ".bak").c_str()); remove("save_test_bad.sav"); remove("save_test_old.sav");
  printf("save_test: %s (%d failures)\n", fails ? "FAIL" : "ok", fails);
  return fails ? 1 : 0;
}
