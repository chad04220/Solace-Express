// Autoland sweep: every aircraft at every airport it can use, in calm, crosswind and gusty weather, from several
// directions and heights. A run succeeds when the autopilot lands on the runway (touchdown under 3 m/s, stopped on the
// runway) without crashing, within 30 simulated minutes.
//   autoland_sweep [slice count] [--csv file]   (slice/count: run every count-th case starting at slice, for parallel runs)
#include "../src/aircraft.h"
#include "../src/career.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>

int main(int argc, char** argv) {
  int slice = 0, count = 1; const char* csv = nullptr;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--csv") && i + 1 < argc) csv = argv[++i];
    else if (i + 1 < argc && argv[i][0] != '-') { slice = atoi(argv[i]); count = std::max(1, atoi(argv[++i])); }
  }
  g_world.build(); buildStory();
  FILE* out = csv ? fopen(csv, "w") : nullptr;
  if (out) fprintf(out, "aircraft,airport,wind,start,result,seconds,touchdown_mps,along_m,cross_m,go_arounds,max_g,reason\n");
  int n = 0, ok = 0, idx = 0;
  const int nAp = (int)g_world.airports.size();
  for (int si = 0; si <= kWraith; si++) {
    const AircraftSpec& s = kAircraft[si];
    for (int ai = 0; ai < nAp; ai++) {
      const Airport& A = g_world.airports[ai];
      if (!s.special && !runwayOK(s, A)) continue;   // the career never sends it there
      if (s.special && (A.length < 1400.f || A.surface != SURF_ASPHALT) && si == kResearchJet) continue;   // (the XR-30 needs a long hard runway)
      for (int wi = 0; wi < 3; wi++)
        for (int st = 0; st < 2; st++, idx++) {
          if (idx % count != slice) continue;
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
          p.apEngage(Plane::AP_NAV, ai, wx);
          float tdVs = 0, maxG = 1; bool td = false; int k = 0, goArounds = 0, last = 0;
          for (; k < 1800 * 60 && !p.ev.crashed && !p.apDone; k++) {
            p.step(1 / 60.f, wx, k / 60.f);
            maxG = std::max(maxG, p.gLoad);
            if (p.ev.touchdown && !td) { td = true; tdVs = -p.ev.touchdownVs; }
            if (p.apStage == Plane::APS_GOAROUND && last != Plane::APS_GOAROUND) goArounds++;
            last = p.apStage;
          }
          vec3 rel = p.pos - A.pos();
          float along = fabsf(dot(rel, A.dir())), cross = fabsf(dot(rel, vec3(-A.dir().z, 0, A.dir().x)));
          bool good = !p.ev.crashed && p.apDone && td && tdVs < 3.f && along < A.length * 0.5f && cross < A.width * 0.5f;
          const char* why = p.ev.crashed ? p.ev.crashReason.c_str() : !p.apDone ? "timeout" : !td ? "no touchdown" : tdVs >= 3.f ? "hard" : "off runway";
          printf("%-16s %s wind%d start%d  %s  %5.0f s  td %.1f  along %5.0f  cross %4.1f  GA %d  maxG %.1f  %s\n", s.name, A.code, wi, st,
                 good ? "ok  " : "FAIL", k / 60.f, tdVs, along, cross, goArounds, maxG, good ? "" : why);
          fflush(stdout);
          if (out) fprintf(out, "%s,%s,%d,%d,%s,%.0f,%.2f,%.0f,%.1f,%d,%.2f,%s\n", s.id, A.code, wi, st, good ? "ok" : "fail", k / 60.f, tdVs, along, cross, goArounds, maxG, good ? "" : why);
          n++; ok += good;
        }
    }
  }
  printf("autoland sweep: %d / %d landed\n", ok, n);
  if (out) fclose(out);
  return ok == n ? 0 : 1;
}
