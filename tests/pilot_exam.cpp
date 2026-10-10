// The pilot's checkride (docs/PILOT.md): every aircraft at every field it may use, from two directions, in calm air, a
// crosswind, gusts, a wind that shifts on the final, and a storm. Each flight is flown by whatever answers
// Plane::apEngage(AP_NAV, field) and scored as an examiner would: did it land and stop on the runway, was the approach
// stabilised, how was the touchdown, how smooth was the ride, how close did it come to the ground and the stall, how
// many go-arounds, how long and how much fuel. The present autopilot's score is the baseline the new pilot must beat.
//   pilot_exam [slice count] [--csv file] [--craft i] [--airport CODE] [--wind w] [--comfort] [--quiet]
//   (wind: 0 calm, 1 crosswind, 2 gusts, 3 wind shift on the final, 4 storm)
#include "../src/aircraft.h"
#include "../src/career.h"
#include "../src/weather.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {
const int kWinds = 5;
const char* kWindName[kWinds] = {"calm", "cross", "gusts", "shift", "storm"};

Weather caseWeather(int wi, int st, const Airport& A) {
  Weather wx;
  if (wi == 0) { wx.windSpeed = 0.5f; wx.turbulence = 0.05f; }
  else if (wi == 1) { wx.windSpeed = 5.f; wx.windFrom = wrapDeg360(A.heading + 70.f); wx.turbulence = 0.2f; }   // ~10 kt across
  else if (wi == 2) { wx.windSpeed = 8.f; wx.gust = 4.f; wx.windFrom = wrapDeg360(A.heading + 120.f + st * 90.f); wx.turbulence = 0.45f; }
  else if (wi == 3) { wx.windSpeed = 2.f; wx.windFrom = wrapDeg360(A.heading + 20.f); wx.turbulence = 0.15f; }    // (turns and builds near the field: below)
  else { wx.windSpeed = 9.f; wx.gust = 6.f; wx.windFrom = wrapDeg360(A.heading + 40.f + st * 60.f); wx.turbulence = 0.6f;
         wx.cloudCover = 0.85f; wx.cloudBase = 700.f; wx.precip = 1; wx.storm = true; wx.visibility = 6000.f; }
  return wx;
}

struct Sample { float t, x, z, h, ias, vs, bank, gnd; };   // (h: above the field; gnd: the ground speed)

struct Score {
  bool landed = false, stopped = false, crashed = false, declined = false, timeout = false;
  float seconds = 0, fuelKg = 0, tdSink = 0, tdZone = 0, tdCross = 0, tdCrab = 0;
  bool stabilised = false; float stabSpeedSd = 0, stabMaxSink = 0, stabPathRms = 0, stabBankLow = 0;
  float gMax = 1, gMin = 1, gRms = 0, jerkRms = 0, bankMax = 0, pitchMax = 0, pitchMin = 0;
  float minClear = 1e9f, minStall = 1e9f;
  int goArounds = 0;
  std::string why;
};

float pct(std::vector<float> v, float p) { if (v.empty()) return 0.f; std::sort(v.begin(), v.end()); return v[std::min(v.size() - 1, (size_t)(p * (v.size() - 1) + 0.5f))]; }
}  // namespace

int main(int argc, char** argv) {
  int slice = 0, count = 1, onlyCraft = -1, onlyWind = -1; bool comfort = false, quiet = false; const char* csv = nullptr; const char* onlyAp = nullptr;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--csv") && i + 1 < argc) csv = argv[++i];
    else if (!strcmp(argv[i], "--craft") && i + 1 < argc) onlyCraft = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--wind") && i + 1 < argc) onlyWind = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--airport") && i + 1 < argc) onlyAp = argv[++i];
    else if (!strcmp(argv[i], "--comfort")) comfort = true;
    else if (!strcmp(argv[i], "--quiet")) quiet = true;
    else if (i + 1 < argc && argv[i][0] != '-') { slice = atoi(argv[i]); count = std::max(1, atoi(argv[++i])); }
  }
  g_world.build(); buildStory();
  FILE* out = csv ? fopen(csv, "w") : nullptr;
  if (out) fprintf(out, "aircraft,airport,wind,start,result,seconds,fuel_kg,go_arounds,stabilised,stab_speed_sd,stab_max_sink,stab_path_rms,stab_bank_low,"
                        "td_sink,td_zone,td_cross,td_crab,g_max,g_min,g_rms,jerk_rms,bank_max,pitch_max,pitch_min,min_clear,min_stall,why\n");
  std::vector<Score> all; std::vector<int> allCraft;
  const int nAp = (int)g_world.airports.size();
  int idx = 0;
  const float dt = 1 / 60.f;
  for (int si = 0; si <= kWraith; si++) {
    const AircraftSpec& s = kAircraft[si];
    if (comfort && si >= kNumAircraft) continue;
    for (int ai = 0; ai < nAp; ai++) {
      const Airport& A = g_world.airports[ai];
      if (!runwayOK(s, A)) continue;   // (the fields the career sends it to)
      for (int wi = 0; wi < kWinds; wi++)
        for (int st = 0; st < 2; st++, idx++) {
          if (idx % count != slice) continue;
          if ((onlyCraft >= 0 && si != onlyCraft) || (onlyWind >= 0 && wi != onlyWind) || (onlyAp && strcmp(onlyAp, A.code) != 0)) continue;
          Weather wx = caseWeather(wi, st, A); const Weather wx0 = wx;
          float brg = (st ? 215.f : 35.f) + ai * 23.f, dist = st ? 9000.f : 16000.f;
          vec3 dir(sinf(brg * DEG), 0, -cosf(brg * DEG));
          vec3 start = A.pos() + dir * dist;
          start.x = clampf(start.x, -WORLD_HALF * 0.95f, WORLD_HALF * 0.95f); start.z = clampf(start.z, -WORLD_HALF * 0.95f, WORLD_HALF * 0.95f);
          start.y = std::max(A.elev + (st ? 600.f : 1500.f), g_world.height(start.x, start.z) + 450.f);
          Plane p; p.reset(&s, start, wrapDeg360(brg + 90.f), s.maxFuel * 0.7f, 150.f, true, s.cruise * 0.8f);
          p.ctl.gearDown = !s.retract; p.gear = p.ctl.gearDown ? 1.f : 0.f; p.ctl.throttle = 0.7f;
          p.apComfort = comfort;
          const float fuel0 = p.fuel;
          p.apEngage(Plane::AP_NAV, ai, wx);
          Score sc;
          if (!p.apDecline.empty()) { sc.declined = true; sc.why = p.apDecline; }
          std::vector<Sample> trace; trace.reserve(1800 * 4);
          float gPrev = 1.f, gF = 1.f, jerkSum = 0, gSum = 0; int nAir = 0, holdK = 0, lastStage = -1; bool td = false; int k = 0;
          float shiftT = -1.f;
          for (; !sc.declined && k < 1800 * 60 && !p.ev.crashed && !p.apDone && holdK < 180 * 60; k++) {
            holdK = p.apMode == Plane::AP_HOLD && p.apHoldFor >= 0 ? holdK + 1 : 0;
            const float dField = length(vec3(p.pos.x - A.pos().x, 0, p.pos.z - A.pos().z));
            if (wi == 3) {   // within 8 km of the field the wind turns 120 deg and builds to 8 m/s over two minutes
              if (shiftT < 0.f && dField < 8000.f) shiftT = k * dt;
              const float f = shiftT < 0.f ? 0.f : clampf((k * dt - shiftT) / 120.f, 0.f, 1.f);
              wx.windFrom = wrapDeg360(wx0.windFrom + 120.f * f); wx.windSpeed = wx0.windSpeed + (8.f - wx0.windSpeed) * f; wx.gust = 2.f * f;
            }
            p.step(dt, wx, k * dt);
            const float terrain = g_world.height(p.pos.x, p.pos.z);
            if (!p.onGround && k > 60) {
              nAir++;
              gF += (p.gLoad - gF) * std::min(1.f, dt * 6.f);   // (the g a body feels: the vibration above ~1 Hz filtered off)
              const float jerk = (gF - gPrev) / dt; gPrev = gF;
              jerkSum += jerk * jerk; gSum += (gF - 1.f) * (gF - 1.f);
              sc.gMax = std::max(sc.gMax, p.gLoad); sc.gMin = std::min(sc.gMin, p.gLoad);
              sc.bankMax = std::max(sc.bankMax, fabsf(p.bankDeg())); sc.pitchMax = std::max(sc.pitchMax, p.pitchDeg()); sc.pitchMin = std::min(sc.pitchMin, p.pitchDeg());
              // (the ground: away from the field's own approach and departure, 3 km round it)
              if (dField > 3000.f) sc.minClear = std::min(sc.minClear, p.pos.y - std::max(terrain, 0.f));
              const ApEnvelope& E = p.apEnv;
              const float vs = E.vs1 + (E.vs0 - E.vs1) * clampf(p.flaps, 0.f, 1.f);
              if (p.pos.y - std::max(terrain, A.elev) > 15.f && vs > 1.f) sc.minStall = std::min(sc.minStall, p.ias / vs);
            }
            if (p.apStage == Plane::APS_GOAROUND && lastStage != Plane::APS_GOAROUND) sc.goArounds++;
            lastStage = p.apStage;
            if ((k % 15) == 0 && !td) trace.push_back({k * dt, p.pos.x, p.pos.z, p.pos.y - p.gearHeight() - A.elev, p.ias, p.vel.y, p.bankDeg(), length(vec3(p.vel.x, 0, p.vel.z))});
            if (p.ev.touchdown && !td) {
              td = true; sc.tdSink = -p.ev.touchdownVs;
              // the runway end it landed on: the one its ground track points down
              vec3 v2(p.vel.x, 0, p.vel.z); const bool rev = dot(v2, A.dir()) < 0.f; vec3 ld = rev ? A.dir() * -1.f : A.dir();
              vec3 rel = p.pos - A.threshold(rev); rel.y = 0;
              sc.tdZone = dot(rel, ld); sc.tdCross = fabsf(dot(rel, vec3(-ld.z, 0, ld.x)));
              sc.tdCrab = fabsf(wrapAngle((p.heading() - atan2f(ld.x, -ld.z) / DEG) * DEG) / DEG);
            }
          }
          sc.seconds = k * dt; sc.fuelKg = fuel0 - p.fuel; sc.crashed = p.ev.crashed;
          if (nAir > 0) { sc.gRms = sqrtf(gSum / nAir); sc.jerkRms = sqrtf(jerkSum / nAir); }
          if (holdK >= 180 * 60 && !p.ev.crashed) { sc.declined = true; sc.why = "declined in the air: " + p.apDecline; }
          if (!sc.declined) {
            sc.timeout = !p.apDone && !p.ev.crashed;
            vec3 rel = p.pos - A.pos();
            const float along = fabsf(dot(rel, A.dir())), cross = fabsf(dot(rel, vec3(-A.dir().z, 0, A.dir().x)));
            sc.landed = td && !p.ev.crashed && sc.tdSink < 3.f;
            sc.stopped = sc.landed && p.apDone && !p.apOverrun && along < A.length * 0.5f && cross < A.width * 0.5f;
            sc.why = p.ev.crashed ? p.ev.crashReason : sc.timeout ? "timeout" : !td ? "no touchdown" : sc.tdSink >= 3.f ? "hard" : !sc.stopped ? "off the runway" : "";
          }
          // the approach: from 300 m above the field to 30 m, inside the final's cone of the runway end it landed on
          if (td && !trace.empty()) {
            vec3 v2(p.vel.x, 0, p.vel.z); const bool rev = dot(v2, A.dir()) < 0.f; vec3 ld = rev ? A.dir() * -1.f : A.dir(), th = A.threshold(rev);
            std::vector<Sample> win;
            for (auto& q : trace) {
              vec3 r(q.x - th.x, 0, q.z - th.z);
              const float al = -dot(r, ld), cr = fabsf(dot(r, vec3(-ld.z, 0, ld.x)));
              if (q.h <= 300.f && q.h >= 30.f && al > 0.f && al < 9000.f && cr < 150.f + al * 0.15f) win.push_back(q);
            }
            // (only the last continuous descent to the runway: a go-around's earlier attempt is not this approach)
            for (size_t i = win.size(); i-- > 1;) if (win[i].t - win[i - 1].t > 1.f) { win.erase(win.begin(), win.begin() + i); break; }
            if (win.size() >= 8) {
              double m = 0; for (auto& q : win) m += q.ias; m /= win.size();
              double v = 0; for (auto& q : win) v += (q.ias - m) * (q.ias - m);
              sc.stabSpeedSd = (float)sqrt(v / win.size());
              // height against the distance flown: a straight line is a steady path; its residual is the porpoising
              double sx = 0, sy = 0, sxx = 0, sxy = 0; const int n = (int)win.size();
              std::vector<float> dd(n); float acc = 0;
              for (int i = 0; i < n; i++) { if (i) acc += win[i].gnd * (win[i].t - win[i - 1].t); dd[i] = acc; }
              for (int i = 0; i < n; i++) { sx += dd[i]; sy += win[i].h; sxx += dd[i] * dd[i]; sxy += dd[i] * win[i].h; }
              const double den = n * sxx - sx * sx, b = den != 0 ? (n * sxy - sx * sy) / den : 0, a = (sy - b * sx) / n;
              double rr = 0; for (int i = 0; i < n; i++) { double e = win[i].h - (a + b * dd[i]); rr += e * e; }
              sc.stabPathRms = (float)sqrt(rr / n);
              for (auto& q : win) { sc.stabMaxSink = std::max(sc.stabMaxSink, -q.vs); if (q.h < 150.f) sc.stabBankLow = std::max(sc.stabBankLow, fabsf(q.bank)); }
              // (stabilised as the airlines define it: the speed held within 5 kt, the sink under 1,000 fpm - a steep approach
              // flown by design, the research craft's, up to its own limit - the path steady, the wings near level low down)
              const float sinkLim = std::max(5.1f, p.apEnv.descentMax + 0.5f);
              sc.stabilised = sc.stabSpeedSd < 2.6f && sc.stabMaxSink < sinkLim && sc.stabPathRms < 12.f && sc.stabBankLow < 15.f;
            }
          }
          const char* res = sc.declined ? "declined" : sc.stopped ? "ok" : "FAIL";
          if (!quiet)
            printf("%-16s %s %-5s %d  %-8s %5.0f s  fuel %6.1f  GA %d  stab %d (sd %.1f sink %.1f path %4.1f bank %2.0f)  td %.2f m/s %4.0f m %4.1f m crab %3.0f  g %.2f..%.2f rms %.3f jerk %.2f  bank %3.0f  pitch %+3.0f/%+3.0f  clear %4.0f  stall %.2f  %s\n",
                   s.name, A.code, kWindName[wi], st, res, sc.seconds, sc.fuelKg, sc.goArounds, sc.stabilised, sc.stabSpeedSd, sc.stabMaxSink, sc.stabPathRms, sc.stabBankLow,
                   sc.tdSink, sc.tdZone, sc.tdCross, sc.tdCrab, sc.gMin, sc.gMax, sc.gRms, sc.jerkRms, sc.bankMax, sc.pitchMax, sc.pitchMin, sc.minClear > 1e8f ? -1.f : sc.minClear,
                   sc.minStall > 1e8f ? -1.f : sc.minStall, sc.why.c_str());
          fflush(stdout);
          if (out) {
            std::string why = sc.why; for (char& c : why) if (c == ',' || c == '"' || c == '\n') c = ';';
            fprintf(out, "%s,%s,%s,%d,%s,%.1f,%.2f,%d,%d,%.3f,%.3f,%.3f,%.2f,%.3f,%.1f,%.2f,%.1f,%.3f,%.3f,%.4f,%.4f,%.1f,%.1f,%.1f,%.1f,%.3f,%s\n",
                    s.id, A.code, kWindName[wi], st, res, sc.seconds, sc.fuelKg, sc.goArounds, sc.stabilised, sc.stabSpeedSd, sc.stabMaxSink, sc.stabPathRms, sc.stabBankLow,
                    sc.tdSink, sc.tdZone, sc.tdCross, sc.tdCrab, sc.gMax, sc.gMin, sc.gRms, sc.jerkRms, sc.bankMax, sc.pitchMax, sc.pitchMin,
                    sc.minClear > 1e8f ? -1.f : sc.minClear, sc.minStall > 1e8f ? -1.f : sc.minStall, why.c_str());
            fflush(out);
          }
          all.push_back(sc); allCraft.push_back(si);
        }
    }
  }
  // the scorecard
  int n = (int)all.size(), ok = 0, dec = 0, stab = 0, landed = 0, ga = 0;
  std::vector<float> sink, zone, cross, grms, jerk, clear, stall;
  for (auto& sc : all) {
    ok += sc.stopped; dec += sc.declined; landed += sc.landed; ga += sc.goArounds;
    if (sc.landed) { stab += sc.stabilised; sink.push_back(sc.tdSink); zone.push_back(sc.tdZone); cross.push_back(sc.tdCross); }
    if (!sc.declined) { grms.push_back(sc.gRms); jerk.push_back(sc.jerkRms); if (sc.minClear < 1e8f) clear.push_back(sc.minClear); if (sc.minStall < 1e8f) stall.push_back(sc.minStall); }
  }
  printf("\npilot exam: %d flights: %d landed and stopped on the runway, %d declined, %d other; %d go-arounds\n", n, ok, dec, n - ok - dec, ga);
  printf("  stabilised approaches %d of %d landings\n", stab, landed);
  printf("  touchdown sink m/s: median %.2f, p90 %.2f, worst %.2f   zone m past the threshold: median %.0f, p10 %.0f, p90 %.0f   centreline m: median %.1f, p90 %.1f\n",
         pct(sink, 0.5f), pct(sink, 0.9f), pct(sink, 1.f), pct(zone, 0.5f), pct(zone, 0.1f), pct(zone, 0.9f), pct(cross, 0.5f), pct(cross, 0.9f));
  printf("  ride: g rms median %.3f p90 %.3f   jerk rms median %.2f p90 %.2f g/s\n", pct(grms, 0.5f), pct(grms, 0.9f), pct(jerk, 0.5f), pct(jerk, 0.9f));
  printf("  margins: ground clearance p10 %.0f m, worst %.0f m   stall margin (IAS / stall) p10 %.2f, worst %.2f\n", pct(clear, 0.1f), pct(clear, 0.f), pct(stall, 0.1f), pct(stall, 0.f));
  if (out) fclose(out);
  return 0;
}
