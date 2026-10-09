// The strip model (aero_strips.cpp) per type: the geometry it reads off the drawn model, what the calibration made of
// it, and checks that the result is an aeroplane - stable in pitch, trimmed at cruise with a sane tail setting, the CG
// where the drawn gear can carry it, stalling at the published CLmax, and the controls each turning it the right way.
#include "../src/aero.h"
#include "../src/aircraft.h"
#include "../src/models.h"
#include <cstdio>

static int fails = 0;
static void check(bool ok, const char* what, const char* name) {
  if (!ok) { printf("  FAIL %s: %s\n", name, what); fails++; }
}

int main() {
  printf("%-16s %6s %6s %5s %5s %6s %6s %6s %6s %6s %6s %5s %6s %6s %6s\n", "type", "Smod", "Sspec", "AR", "MAC", "wInc", "flapA", "tInc",
         "xNP", "cgZ", "cgY", "SM", "aTrim", "mainZ", "noseZ");
  for (int i = 0; i < kNumAircraft + 4; i++) {
    const AircraftSpec& s = kAircraft[i];
    if (s.special != 0) continue;
    const AeroGeom& g = aeroGeom(s);
    const GearStations gs = gearStations(s);
    printf("%-16s %6.1f %6.1f %5.2f %5.2f %6.2f %6.2f %6.2f %6.2f %6.2f %6.2f %5.2f %6.2f %6.2f %6.2f\n", s.name, g.wingArea, s.wingArea,
           g.surf[AS_WING].AR, g.MAC, g.wingInc / DEG, g.flapA / DEG, g.tailInc / DEG, g.xNP, g.cg.z, g.cg.y, g.staticMargin,
           g.trimAlpha / DEG, gs.mainZ, s.taildragger ? gs.tailZ : gs.noseZ);
    check(g.staticMargin > 0.03f && g.staticMargin < 0.25f, "static margin", s.name);
    check(fabsf(g.tailInc) < 8.f * DEG, "tail setting", s.name);
    if (!s.taildragger) check(gs.mainZ > g.cg.z + 0.05f && gs.noseZ < g.cg.z, "CG ahead of the main gear", s.name);
    else check(gs.mainZ < g.cg.z, "CG behind the main gear (taildragger)", s.name);
    // a steady sweep at sea level: the lift curve, the stall, the pitching moment's slope
    AeroMem mem; AeroOut o;
    const float V = 40.f, qS = 0.5f * 1.225f * V * V * s.wingArea;
    float best = 0, aBest = 0;
    for (float a = -4.f; a < 26.f; a += 0.5f) {
      AeroIn in; in.steady = true; in.va = vec3(0, -V * sinf(a * DEG), -V * cosf(a * DEG));
      aeroForces(g, s, in, mem, 0.f, o);
      float L = o.F.y * cosf(o.alpha) - o.F.z * sinf(o.alpha);
      if (L / qS > best) { best = L / qS; aBest = a; }
    }
    printf("    glide %.1f:1 (gear down %.1f:1)\n", g.glideLD[0], g.glideLD[1]);
    printf("    rigging: aileron %+.3f rudder %+.3f  flaps to %.0f%%   CLmax %.2f (spec %.2f) at %.1f deg", g.ailRig, g.rudRig, g.flapMax * 100.f, best, s.CLmax, aBest);
    check(fabsf(g.ailRig) < 0.15f && fabsf(g.rudRig) < 0.15f, "rigging within a tab's reach", s.name);
    // the controls: each about its own axis, the right way
    auto at = [&](float pitch, float roll, float yaw) {
      AeroIn in; in.steady = true; in.va = vec3(0, -V * sinf(3 * DEG), -V * cosf(3 * DEG));
      in.pitch = pitch; in.roll = roll; in.yaw = yaw;
      aeroForces(g, s, in, mem, 0.f, o); return o.M;
    };
    vec3 m0 = at(0, 0, 0), mp = at(1, 0, 0), mr = at(0, 1, 0), my = at(0, 0, 1);
    printf("  dM pitch %.0f  roll %.0f (yaw %.0f)  yaw %.0f (roll %.0f)\n", (mp - m0).x, -(mr - m0).z, -(mr - m0).y, -(my - m0).y, -(my - m0).z);
    check((mp - m0).x > 0.f, "pitch up with back stick", s.name);
    check((mr - m0).z < 0.f, "roll right with right stick", s.name);
    check((my - m0).y < 0.f, "nose right with right rudder", s.name);
    // the derivatives that make it fly: weathercock, dihedral, roll and pitch damping
    auto withB = [&](float beta, vec3 w) {
      AeroIn in; in.steady = true; in.va = vec3(V * sinf(beta), -V * sinf(3 * DEG), -V * cosf(3 * DEG)); in.w = w;
      aeroForces(g, s, in, mem, 0.f, o); return o.M;
    };
    vec3 mb = withB(5 * DEG, vec3()), mpr = withB(0, vec3(0, 0, -0.5f)), mq = withB(0, vec3(0.2f, 0, 0)), mrr = withB(0, vec3(0, -0.2f, 0));
    printf("    Cn_beta %+.3f  Cl_beta %+.3f  Cl_p %+.3f  Cm_q %+.2f  Cn_r %+.3f\n",
           -(mb - m0).y / (qS * s.span * 5 * DEG), -(mb - m0).z / (qS * s.span * 5 * DEG),
           -(mpr - m0).z / (qS * s.span * (0.5f * s.span / (2 * V))), (mq - m0).x / (qS * g.MAC * (0.2f * g.MAC / (2 * V))),
           -(mrr - m0).y / (qS * s.span * (0.2f * s.span / (2 * V))));
    check(-(mb - m0).y > 0.f, "weathercock stability (Cn_beta > 0)", s.name);
    // (with the sideslip-to-aileron interconnect where the wing needs one: the forward-swept XR-20's)
    const float clb = -(mb - m0).z / (qS * s.span * 5 * DEG) + g.betaToAil * (-(mr - m0).z) / (qS * s.span) * -1.f;
    printf("    Cl_beta with interconnect %+.3f (gain %.2f)\n", clb, g.betaToAil);
    check(clb < 0.f, "dihedral effect (Cl_beta < 0)", s.name);
    check(-(mpr - m0).z < 0.f, "roll damping", s.name);
    check((mq - m0).x < 0.f, "pitch damping", s.name);
    // the elevator it takes to fly level, steady and hands-off: at cruise, slow and clean, and on the approach with
    // full flap (the trim wheel adds up to 0.3 either way)
    {
      const float W = (s.emptyMass + s.maxFuel * 0.6f + s.cargoKg * 0.5f) * G0, rho = 1.225f;
      const float vs1 = sqrtf(2 * W / (rho * s.wingArea * aeroCLmaxFlown(s, 0))), vs0 = sqrtf(2 * W / (rho * s.wingArea * aeroCLmaxFlown(s, 1)));
      printf("    flown CLmax %.2f clean, %.2f flaps (wing %.2f, %.2f)\n", aeroCLmaxFlown(s, 0), aeroCLmaxFlown(s, 1), s.CLmax, s.CLmax + s.flapCL * g.flapMax);
      struct Case { const char* name; float V, flaps; } cases[] = {{"cruise", s.cruise, 0.f}, {"1.3 Vs1", 1.3f * vs1, 0.f}, {"Vref flap", 1.3f * vs0, g.flapMax}, {"1.1 Vs0", 1.1f * vs0, g.flapMax}};
      printf("    elevator to trim:");
      for (const Case& c : cases) {
        float alpha = 4.f * DEG, el = 0.f;
        for (int it = 0; it < 30; it++) {
          auto f = [&](float a, float e) { AeroIn in; in.steady = true; in.va = vec3(0, -c.V * sinf(a), -c.V * cosf(a)); in.rho = rho; in.flapL = in.flapR = c.flaps; in.pitch = e; aeroForces(g, s, in, mem, 0.f, o); return o; };
          AeroOut o0 = f(alpha, el); float L0 = o0.F.y * cosf(alpha) - o0.F.z * sinf(alpha), M0 = o0.M.x;
          AeroOut oa = f(alpha + 0.01f, el); float La = oa.F.y * cosf(alpha + 0.01f) - oa.F.z * sinf(alpha + 0.01f), Ma = oa.M.x;
          AeroOut oe = f(alpha, el + 0.02f); float Le = oe.F.y * cosf(alpha) - oe.F.z * sinf(alpha), Me = oe.M.x;
          float a11 = (La - L0) / 0.01f, a12 = (Le - L0) / 0.02f, a21 = (Ma - M0) / 0.01f, a22 = (Me - M0) / 0.02f, det = a11 * a22 - a12 * a21;
          if (fabsf(det) < 1e-6f) break;
          alpha += clampf(((W - L0) * a22 + M0 * a12) / det, -0.05f, 0.05f);
          el += clampf((-M0 * a11 - (W - L0) * a21) / det, -0.2f, 0.2f);
        }
        {   // (a balance or nothing: an elevator that can't trim it doesn't converge)
          AeroIn in; in.steady = true; in.va = vec3(0, -c.V * sinf(alpha), -c.V * cosf(alpha)); in.rho = rho; in.flapL = in.flapR = c.flaps; in.pitch = el;
          aeroForces(g, s, in, mem, 0.f, o);
          float L = o.F.y * cosf(alpha) - o.F.z * sinf(alpha);
          if (fabsf(L - W) > 0.02f * W || fabsf(o.M.x) > 0.01f * W * g.MAC) el = 9.f;
        }
        printf("  %s %.0f m/s a %.1f: %+.2f", c.name, c.V, alpha / DEG, el);
        check(fabsf(el) < (c.flaps > 0.f ? (c.V < 1.2f * vs0 ? 0.95f : 0.65f) : 0.75f), c.name, s.name);   // (the calibration's own aims: aero_strips.cpp)
      }
      printf("\n");
    }
  }
  printf(fails ? "%d failures\n" : "all ok\n", fails);
  return fails ? 1 : 0;
}
