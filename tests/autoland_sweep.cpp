// Autoland sweep: every aircraft at every airport it can use, in calm, crosswind and gusty weather, from several
// directions and heights. A run succeeds when the autopilot lands on the runway (touchdown under 3 m/s, stopped on the
// runway) without crashing, within 30 simulated minutes.
//   autoland_sweep [slice count] [--csv file] [--craft i] [--airport CODE] [--wind w] [--comfort] [--all]
//   (--comfort: the gentle guidance for passengers and fragile loads, apComfort, on the career types; --all: every field, as the GPS offers them, not only
//   those the career would send the type to - a short one must be refused, never overrun)
//   (slice/count: run every count-th case starting at slice, for parallel runs; the others narrow the cases)
//   (env APDBG: the plans, go-arounds and the last metres; APTRACE: the guidance stage and leg every 10 s)
#include "../src/aircraft.h"
#include "../src/career.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <cmath>

int main(int argc, char** argv) {
  int slice = 0, count = 1, onlyCraft = -1, onlyWind = -1; bool comfort = false, all = false; const char* csv = nullptr; const char* onlyAp = nullptr;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--csv") && i + 1 < argc) csv = argv[++i];
    else if (!strcmp(argv[i], "--craft") && i + 1 < argc) onlyCraft = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--wind") && i + 1 < argc) onlyWind = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--airport") && i + 1 < argc) onlyAp = argv[++i];
    else if (!strcmp(argv[i], "--comfort")) comfort = true;
    else if (!strcmp(argv[i], "--all")) all = true;
    else if (i + 1 < argc && argv[i][0] != '-') { slice = atoi(argv[i]); count = std::max(1, atoi(argv[++i])); }
  }
  g_world.build(); buildStory();
  FILE* out = csv ? fopen(csv, "w") : nullptr;
  if (out) fprintf(out, "aircraft,airport,wind,start,result,seconds,touchdown_mps,along_m,cross_m,go_arounds,max_g,reason\n");
  int n = 0, ok = 0, declined = 0, idx = 0;
  const int nAp = (int)g_world.airports.size();
  for (int si = 0; si <= kWraith; si++) {
    const AircraftSpec& s = kAircraft[si];
    if (comfort && si >= kNumAircraft) continue;   // (the gentle law flies passengers and fragile loads: career types only)
    for (int ai = 0; ai < nAp; ai++) {
      const Airport& A = g_world.airports[ai];
      if (!all && !runwayOK(s, A)) continue;   // the career never sends it there (and the autoland refuses it)
      for (int wi = 0; wi < 3; wi++)
        for (int st = 0; st < 2; st++, idx++) {
          if (idx % count != slice) continue;
          if ((onlyCraft >= 0 && si != onlyCraft) || (onlyWind >= 0 && wi != onlyWind) || (onlyAp && strcmp(onlyAp, A.code) != 0)) continue;
          Weather wx;
          if (wi == 0) { wx.windSpeed = 0.5f; wx.turbulence = 0.05f; }
          else if (wi == 1) { wx.windSpeed = 5.f; wx.windFrom = wrapDeg360(A.heading + 70.f); wx.turbulence = 0.2f; }   // ~10 kt across
          else { wx.windSpeed = 8.f; wx.gust = 4.f; wx.windFrom = wrapDeg360(A.heading + 120.f + st * 90.f); wx.turbulence = 0.45f; }   // gusty
          float brg = (st ? 215.f : 35.f) + ai * 23.f, dist = st ? 9000.f : 16000.f;
          vec3 dir(sinf(brg * DEG), 0, -cosf(brg * DEG));
          vec3 start = A.pos() + dir * dist;
          start.x = clampf(start.x, -WORLD_HALF * 0.95f, WORLD_HALF * 0.95f); start.z = clampf(start.z, -WORLD_HALF * 0.95f, WORLD_HALF * 0.95f);
          start.y = std::max(A.elev + (st ? 600.f : 1500.f), g_world.height(start.x, start.z) + 450.f);
          Plane p; p.reset(&s, start, wrapDeg360(brg + 90.f), s.maxFuel * 0.7f, 150, true, s.cruise * 0.8f);
          p.ctl.gearDown = !s.retract; p.gear = p.ctl.gearDown ? 1.f : 0.f; p.ctl.throttle = 0.7f;
          p.apComfort = comfort;
          p.apEngage(Plane::AP_NAV, ai, wx);
          if (!p.apDecline.empty()) {   // declined before committing to an approach: an explicit refusal, not a landing
            printf("%-16s %s wind%d start%d  DECLINED  %s\n", s.name, A.code, wi, st, p.apDecline.c_str());
            if (out) fprintf(out, "%s,%s,%d,%d,declined,0,0,0,0,0,0,%s\n", s.id, A.code, wi, st, p.apDecline.c_str());
            n++; declined++; continue;
          }
          float tdVs = 0, maxG = 1; bool td = false; int k = 0, goArounds = 0, last = 0;
          for (; k < 1800 * 60 && !p.ev.crashed && !p.apDone; k++) {
            p.step(1 / 60.f, wx, k / 60.f);
            maxG = std::max(maxG, p.gLoad);
            if (p.ev.touchdown && !td) {
              td = true; tdVs = -p.ev.touchdownVs;
              if (getenv("APDBG")) { vec3 r = p.pos - p.apTd; printf("  touchdown %.0f m past the aim point (%.0f m runway), ground speed %.1f m/s (vref %.1f), vs %.1f\n", dot(r, p.apLd), A.length, length(vec3(p.vel.x, 0, p.vel.z)), s.vref, tdVs); }
            }
            if (p.apStage == Plane::APS_GOAROUND && last != Plane::APS_GOAROUND) goArounds++;
            if (getenv("APDBG") && !td && p.apStage == Plane::APS_FINAL && p.pos.y - p.gearHeight() - A.elev < 45.f && (k % 15) == 0) printf("    final: %.1f m up, vs %.2f (asked %.2f), ias %.1f, pitch %.1f, throttle %.2f\n", p.pos.y - p.gearHeight() - A.elev, p.vel.y, p.apVS, p.ias, p.pitchDeg(), p.ctl.throttle);
            if (getenv("APDBG") && p.apStage == Plane::APS_BLEED && (k % 15) == 0) printf("    belly-up: %.1f m up, pitch %.0f, ias %.1f, vs %.1f, g %.1f\n", p.pos.y - p.gearHeight() - A.elev, p.pitchDeg(), p.ias, p.vel.y, p.gLoad);
            if (getenv("APDBG") && !td && p.apStage == Plane::APS_FLARE && (k % 15) == 0) printf("    flare: %.1f m up, vs %.2f, ias %.1f, pitch %.1f, cross %.1f, heading off the runway %.1f\n", p.pos.y - p.gearHeight() - A.elev, p.vel.y, p.ias, p.pitchDeg(), dot(p.pos - p.apTd, vec3(-p.apLd.z, 0, p.apLd.x)), wrapAngle(p.heading() * DEG - atan2f(p.apLd.x, -p.apLd.z)) / DEG);
            if (getenv("APDBG") && td && (k % 60) == 0 && length(vec3(p.vel.x, 0, p.vel.z)) > 3.f) printf("    rollout: %.1f m/s, brake %.2f, on ground %d, throttle %.2f, cross %.1f, heading off the runway %.1f\n", length(vec3(p.vel.x, 0, p.vel.z)), p.ctl.brake, (int)p.onGround, p.ctl.throttle, dot(p.pos - p.apTd, vec3(-p.apLd.z, 0, p.apLd.x)), wrapAngle(p.heading() * DEG - atan2f(p.apLd.x, -p.apLd.z)) / DEG);
            if (getenv("APTRACE") && (p.apStage != last || (k % 600) == 0)) { vec3 r = p.pos - p.apTd; printf("    t %4.0f stage %d leg %d along %6.0f cross %6.0f agl %5.0f above field %5.0f  %s\n", k / 60.f, p.apStage, p.apLeg, dot(r, p.apLd), dot(r, vec3(-p.apLd.z, 0, p.apLd.x)), p.pos.y - g_world.height(p.pos.x, p.pos.z), p.pos.y - A.elev, p.apStatus.c_str()); }
            last = p.apStage;
          }
          if (getenv("APDBG") && td) { const float tdOff = clampf(A.length * 0.12f, 80.f, 300.f); printf("  stopped %.0f m past the threshold (%.0f m runway)\n", dot(p.pos - p.apTd, p.apLd) + tdOff, A.length); }
          vec3 rel = p.pos - A.pos();
          float along = fabsf(dot(rel, A.dir())), cross = fabsf(dot(rel, vec3(-A.dir().z, 0, A.dir().x)));
          bool good = !p.ev.crashed && p.apDone && td && tdVs < 3.f && along < A.length * 0.5f && cross < A.width * 0.5f;
          const char* why = p.ev.crashed ? p.ev.crashReason.c_str() : !p.apDone ? "timeout" : !td ? "no touchdown" : tdVs >= 3.f ? "hard" : p.apOverrun ? "overran" : "off runway";
          printf("%-16s %s wind%d start%d  %s  %5.0f s  td %.1f  along %5.0f  cross %4.1f  GA %d  maxG %.1f  %s\n", s.name, A.code, wi, st,
                 good ? "ok  " : "FAIL", k / 60.f, tdVs, along, cross, goArounds, maxG, good ? "" : why);
          fflush(stdout);
          if (out) fprintf(out, "%s,%s,%d,%d,%s,%.0f,%.2f,%.0f,%.1f,%d,%.2f,%s\n", s.id, A.code, wi, st, good ? "ok" : "fail", k / 60.f, tdVs, along, cross, goArounds, maxG, good ? "" : why);
          n++; ok += good;
        }
    }
  }
  printf("autoland sweep: %d / %d landed, %d declined\n", ok, n, declined);
  if (out) fclose(out);
  return ok + declined == n ? 0 : 1;
}
