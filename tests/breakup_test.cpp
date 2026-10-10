// Breakup: every type comes apart into its own components, each surface goes with the piece it belongs to, the pieces
// weigh what the airframe did, and they fly down on their own air - wings tumbling and drifting, the heavy fuselage
// falling fast, skin panels fluttering - without the integration ever blowing up.
#include "../src/breakup.h"
#include "../src/aircraft.h"
#include "../src/models.h"
#include "../src/aero.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <chrono>

static bool finite3(vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
static float gearHeightFor(const AircraftSpec& s) { Plane p; p.reset(&s, vec3(0, 100, 0), 0, s.maxFuel * 0.5f, 0, true, s.cruise); return p.gearHeight(); }

int main() {
  setvbuf(stdout, nullptr, _IONBF, 0);
  int failures = 0;
  for (int t = 0; t <= kWraith; t++) {
    const AircraftSpec& s = kAircraft[t];
    const ModelDef& m = kModels[t];
    const float gh = gearHeightFor(s);
    BreakPiece P[kMaxBreakPieces];
    const int n = breakPieces(s, 0.f, gh, P);
    assert(n >= 5 && n <= kMaxBreakPieces && P[n - 1].kind == BK_CENTRE);
    int count[BK_COUNT] = {};
    for (int i = 0; i < n; i++) { count[P[i].kind]++; assert(P[i].H.x > 0 && P[i].H.y > 0 && P[i].H.z > 0); }
    printf("%-16s %2d pieces:", s.name, n);
    for (int i = 0; i < n; i++) printf(" %s%s", P[i].side < 0 ? "L " : P[i].side > 0 ? "R " : "", breakKindName(P[i].kind));
    printf("\n");
    // each surface goes with its own piece
    const AeroGeom& g = aeroGeom(s);
    if (!s.special) {
      for (int i = 0; i < g.nSt; i++) {
        const AeroStrip& st = g.st[i];
        float hw, hh, cy; modelSection(m, st.r.z, hw, hh, cy);
        const int o = breakOwner(P, n, st.r);
        const bool outboard = fabsf(st.r.x) > hw + 0.15f;
        if (st.surf == AS_WING && outboard && P[o].kind != BK_WING && P[o].kind != BK_NACELLE && P[o].kind != BK_PROP) { printf("  wing strip %d at %.1f,%.1f,%.1f went to the %s\n", i, st.r.x, st.r.y, st.r.z, breakKindName(P[o].kind)); failures++; }
        if (st.surf == AS_WING && outboard && P[o].kind == BK_WING && (P[o].side > 0) != (st.r.x > 0)) { printf("  wing strip %d on the wrong wing\n", i); failures++; }
        if ((st.surf == AS_TAIL || st.surf == AS_CANARD) && outboard && P[o].kind != (st.surf == AS_TAIL ? BK_TAIL : BK_CANARD) && P[o].kind != BK_FIN) { printf("  tail strip %d at %.1f,%.1f,%.1f went to the %s\n", i, st.r.x, st.r.y, st.r.z, breakKindName(P[o].kind)); failures++; }
        if (st.surf == AS_FIN && st.r.y > cy + hh + 0.3f && P[o].kind != BK_FIN && P[o].kind != BK_TAIL) { printf("  fin strip %d at %.1f,%.1f,%.1f went to the %s\n", i, st.r.x, st.r.y, st.r.z, breakKindName(P[o].kind)); failures++; }
      }
      float pr[2][4]; const int np = modelProps(m, pr);
      for (int i = 0; i < np; i++) assert(P[breakOwner(P, n, vec3(pr[i][0], pr[i][1], pr[i][2]))].kind == BK_PROP);
      if (m.gear <= 2) {   // fixed gear: the wheels come away with their legs
        const GearStations gs = gearStations(s);
        if (getenv("BRKDBG")) for (int i = 0; i < n; i++) printf("    %s C %.2f %.2f %.2f H %.2f %.2f %.2f\n", breakKindName(P[i].kind), P[i].C.x, P[i].C.y, P[i].C.z, P[i].H.x, P[i].H.y, P[i].H.z);
        if (getenv("BRKDBG")) printf("    gear point %.2f %.2f %.2f (gh %.2f track %.2f)\n", 0.5f * gs.track, -gh + 0.15f, gs.mainZ, gh, gs.track);
        for (int sd = -1; sd <= 1; sd += 2) assert(P[breakOwner(P, n, vec3(sd * 0.5f * gs.track, -gh + 0.15f, gs.mainZ))].kind == BK_GEAR);
      }
      // the tips, the fin top and the tail cone
      assert(P[breakOwner(P, n, modelWingTip(m))].kind == BK_WING);
      const int ft = breakOwner(P, n, modelFinTop(m)); assert(P[ft].kind == BK_FIN || P[ft].kind == BK_TAIL);
      const int tc = breakOwner(P, n, modelTailTip(m)); assert(P[tc].kind == BK_AFT || P[tc].kind == BK_FIN);
    }
    // the bodies weigh what the airframe did
    const float mass = s.emptyMass + s.cargoKg * 0.5f, payload = s.cargoKg * 0.5f;
    DebrisBody B[kMaxBreakPieces];
    breakBodies(s, P, n, mass, payload, B);
    float sum = 0;
    for (int i = 0; i < n; i++) { assert(B[i].mass > 0 && B[i].I.x > 0 && B[i].I.y > 0 && B[i].I.z > 0 && finite3(B[i].cg)); sum += B[i].mass; }
    assert(fabsf(sum - mass) < 0.05f * mass + 3.f * n);
    // and they fly down: from 2,500 m at the type's speed (the research jets' 300 m/s), flung apart a little and spinning,
    // in still air - where no piece may ever gain energy (the air only takes it) - and every one comes down
    const float V0 = s.special || s.designMach > 1.f ? 300.f : s.cruise;
    float tDown[kMaxBreakPieces] = {}, vEnd[kMaxBreakPieces] = {}, spin[kMaxBreakPieces] = {};
    Rng r(77 + t);
    auto t0 = std::chrono::steady_clock::now(); int steps = 0;
    for (int i = 0; i < n; i++) {
      const DebrisBody& b = B[i];
      quat q; vec3 p = b.cg + vec3(0, 2500, 0);
      vec3 out = P[i].C; out.y = 0; out = length(out) > 0.1f ? normalize(out) : vec3(0, 1, 0);
      vec3 v = vec3(0, 0, -V0) + out * r.range(8.f, 20.f), w = normalize(vec3(r.range(-1, 1), r.range(-1, 1), r.range(-1, 1))) * r.range(2.f, 6.f);
      auto energy = [&]() { vec3 wb = q.conj().rotate(w); return 0.5f * b.mass * dot(v, v) + b.mass * G0 * p.y + 0.5f * (b.I.x * wb.x * wb.x + b.I.y * wb.y * wb.y + b.I.z * wb.z * wb.z); };
      float t = 0, spinSum = 0, E = energy(), gain = 0, t500 = -1; int spinN = 0;
      while (p.y > 0.f && t < 900.f) {
        debrisStep(b, p, v, q, w, vec3(0), isaDensity(p.y), 1.f / 60.f); t += 1.f / 60.f; steps++;
        if (!finite3(p) || !finite3(v) || !finite3(w)) { printf("  %s: not finite at %.1f s\n", breakKindName(P[i].kind), t); failures++; break; }
        const float E1 = energy(); gain = std::max(gain, (E1 - E) / std::max(E, 1.f)); E = std::min(E, E1);
        if (p.y < 600.f) { spinSum += length(w); spinN++; }
        if (t500 < 0.f && p.y < 500.f) t500 = t;
      }
      tDown[i] = t; vEnd[i] = t500 >= 0.f && t > t500 ? 500.f / (t - t500) : -v.y; spin[i] = spinN ? spinSum / spinN : 0.f;   // (vEnd: its average through the last 500 m)
      if (t >= 900.f) { printf("  the %s never came down (%.0f m left)\n", breakKindName(P[i].kind), p.y); failures++; }
      if (gain > 0.002f) { printf("  the %s gained %.1f%% of its energy\n", breakKindName(P[i].kind), gain * 100.f); failures++; }
      // a lightly loaded wing or tail surface never darts down (edge-on, diving, it can come down at a few tens of m/s);
      // a compact heavy piece (an engine in it) falls fast
      float area = 0; for (const auto& pl : b.plates) area += pl.chord * pl.span;
      const float loading = area > 0.f ? b.mass / area : 1e9f;
      if ((P[i].kind == BK_WING || P[i].kind == BK_TAIL || P[i].kind == BK_FIN) && loading < 12.f && vEnd[i] > 45.f) { printf("  the %s (%.0f kg/m^2) came down at %.0f m/s\n", breakKindName(P[i].kind), loading, vEnd[i]); failures++; }
      if (P[i].kind == BK_NACELLE && vEnd[i] < 40.f) { printf("  the nacelle (%.0f kg) came down at only %.0f m/s\n", b.mass, vEnd[i]); failures++; }
    }
    const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count() / std::max(steps, 1);
    printf("  falls:");
    for (int i = 0; i < n; i++) printf(" %s%s %.0fs %.0fm/s %.1frad/s;", P[i].side < 0 ? "L " : P[i].side > 0 ? "R " : "", breakKindName(P[i].kind), tDown[i], vEnd[i], spin[i]);
    printf("  (%.1f us a step)\n", us);
  }
  // a torn skin panel: flutters and drifts down at a few metres a second
  {
    DebrisBody b = debrisPanel(0.5f, 3.f);
    quat q = quat::axisAngle(vec3(1, 0, 0), 0.3f); vec3 p(0, 1000, 0), v(60, -5, 0), w(0.5f, 0, 1);
    float t = 0, vMin = 1e9f, vMax = -1e9f, spinMax = 0;
    while (p.y > 0.f && t < 1200.f) {
      debrisStep(b, p, v, q, w, vec3(0), isaDensity(p.y), 1.f / 60.f); t += 1.f / 60.f;
      assert(finite3(p) && finite3(v) && finite3(w));
      if (t > 20.f) { vMin = std::min(vMin, -v.y); vMax = std::max(vMax, -v.y); spinMax = std::max(spinMax, length(w)); }
    }
    printf("skin panel 0.5 m, 3 kg/m^2: down from 1000 m in %.0f s, sinking %.1f..%.1f m/s, spinning up to %.1f rad/s\n", t, vMin, vMax, spinMax);
    assert(t < 1200.f && vMin > 0.5f && vMax < 25.f);
  }
  if (failures) { printf("breakup_test: %d failures\n", failures); return 1; }
  printf("breakup_test: every type comes apart by component, each surface with its piece, and every piece flies down\n");
  return 0;
}
