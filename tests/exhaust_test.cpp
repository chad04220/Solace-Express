// CPU-only regressions for the same nozzle inputs the shared GLSL field and exhaust lights consume.
#include "../src/exhaust.h"
#include <limits>

static int failures = 0, checks = 0;
static void check(bool ok, const char* name) { ++checks; if (!ok && ++failures < 30) printf("FAIL: %s\n", name); }
static bool close(float a, float b) { return fabsf(a - b) < 2e-5f; }
static bool close(vec3 a, vec3 b) { return length(a - b) < 3e-5f; }
static Plane running(int m) {
  Plane p; p.spec = &kAircraft[m]; p.engineRunning = true; p.fuel = 100.f;
  p.engineSpool = 1.f; p.mach = 1.7f;
  for (float& t : p.podThr) t = 1.f;
  return p;
}
static ExhaustVisual build(const Plane& p) { return buildExhaustVisual(p, kModels[p.spec - kAircraft]); }
static void valid(const ExhaustVisual& e) {
  check(e.count >= 0 && e.count <= 4, "at most four active nozzles");
  for (int i = 0; i < e.count; ++i) {
    check(close(length(exhaustDirection(e, i)), 1.f), "direction is normalized");
    for (int k = 0; k < 4; ++k) check(std::isfinite(e.exit[i][k]) && std::isfinite(e.axis[i][k]) && std::isfinite(e.power[i][k]), "all packed values finite");
    check(e.exit[i][3] >= .05f && e.exit[i][3] <= 2.f && e.axis[i][3] >= .05f && e.axis[i][3] <= 2.f, "nozzle radii bounded");
  }
}
int main() {
  int expected[4] = {2, 2, 1, 4};
  for (int m = 0; m < kAircraftCount; ++m) {
    Plane p = running(m); ExhaustVisual e = build(p); valid(e);
    check(e.count == (isCareerAircraft(m) ? 0 : expected[m - kNightjar]), "fleet capability and exact 2/2/1/4 nozzle counts");
    if (isCareerAircraft(m)) continue;
    for (float spool : {0.f, .019f, .02f, .3f, .7f, .85f, .9f, .925f, 1.f}) {
      p.engineSpool = spool; e = build(p); valid(e);
      check(e.count == (spool < .02f ? 0 : p.spec->engines), "actual spool gates the off state");
      for (int j = 0; j < e.count; ++j) {
        check(close(e.power[j][0], spool), "actual spool reaches every nozzle");
        check(close(e.power[j][1], smoothstepf(.85f, 1.f, spool)), "reheat interval matches thrustAt for every model");
      }
    }
    p = running(m); p.ctl.throttle = 0.f; check(build(p).count == p.spec->engines, "spool lag follows real core state rather than requested throttle");
    p.engineRunning = false; check(build(p).count == 0, "stopped engine cannot leave flame or light");
    p = running(m); p.fuel = 0.f; check(build(p).count == 0, "empty tank cannot leave flame or light");
    p = running(m); p.fuel = std::numeric_limits<float>::quiet_NaN(); check(build(p).count == 0, "invalid fuel fails closed");
    for (int k = 0; k < p.spec->engines; ++k) {
      p = running(m); p.fail.engineHealth[k] = 0.f; e = build(p);
      check(e.count == p.spec->engines - 1, "each failed engine removes exactly its own plume/light");
      for (int j = 0; j < e.count; ++j) check(e.engine[j] != k, "compacted active slots retain the correct physical engine");
      p.fail.engineHealth[k] = .45f; e = build(p);
      check(e.count == p.spec->engines, "partially healthy engine still emits");
      if (m != kWraith) check(close(e.power[k][2], .45f), "partial health attenuates emission");
    }
    p = running(m); for (float& h : p.fail.engineHealth) h = 0.f;
    check(build(p).count == 0, "all failed engines take exact-empty render path");
    p = running(m); p.engineSpool = std::numeric_limits<float>::quiet_NaN(); check(build(p).count == 0, "invalid spool fails closed");
  }
  {
    const auto n = build(running(kNightjar)), m = build(running(kMantis));
    check(close(exhaustPosition(n, 0), vec3(-1.63f, .16f, 5.91f)) && close(exhaustPosition(n, 1), vec3(1.63f, .16f, 5.91f)), "Nightjar plumes start at both actual rear nacelle exits");
    check(close(n.exit[0][3], .27f) && close(n.axis[1][3], .27f), "Nightjar radii match circular bores");
    check(m.count == 1 && close(exhaustPosition(m, 0), vec3(0.f, -.08f, 7.52f)), "Mantis has exactly one actual centerline exit");
    check(close(m.exit[0][3], .4896f) && close(m.axis[0][3], .4896f), "Mantis larger circular bore scales the shared appearance");
    printf("Anchors: XR-10 (+/-1.630,0.160,5.910) radius .270; XR-20 (0,-.080,7.520) radius .4896\n");
  }
  for (int ni = 0; ni <= 10; ++ni) for (int pi = -10; pi <= 10; ++pi) {
    Plane p = running(kResearchJet); p.nozzle = ni/10.f; p.ctl.pitch = pi/10.f;
    const auto e = build(p); valid(e); const float a = p.nozzle*.5f*PI - p.ctl.pitch*.5f;
    const vec3 axis(0, -sinf(a), cosf(a)), bx(1, 0, 0), by(0, cosf(a), sinf(a));
    for (int i = 0; i < 2; ++i) {
      check(close(exhaustPosition(e, i), vec3(i ? .82f : -.82f, -.12f, 7.75f) + axis*1.06f), "XR-30 anchor follows nozzle pivot through every extreme");
      check(close(exhaustDirection(e, i), axis), "XR-30 plume follows the live vectoring angle");
      check(close(dot(axis, bx), 0.f) && close(dot(axis, by), 0.f) && close(cross(axis, bx), by), "XR-30 elliptical transverse basis follows the nozzle at every angle");
      check(close(e.exit[i][3], .36f) && close(e.axis[i][3], .23f), "XR-30 anisotropic bore preserved");
    }
  }
  for (int ti = 0; ti <= 10; ++ti) for (float vane : {-.7f, 0.f, .7f}) for (float yaw : {-.7f, 0.f, .7f}) {
    Plane p = running(kWraith); const float tilt = ti*.05f*PI;
    for (int i = 0; i < 4; ++i) { p.podTilt[i] = tilt; p.podVane[i] = vane; p.podYaw[i] = yaw; p.podThr[i] = .25f + .4f*i; }
    const auto e = build(p); valid(e);
    for (int i = 0; i < 4; ++i) {
      check(close(exhaustPosition(e, i), kWraithPods[i] + vec3(0, -sinf(tilt), cosf(tilt))*1.57f), "all Wraith anchors move with their actual pod");
      check(close(exhaustDirection(e, i), vec3(-sinf(yaw), -sinf(tilt + vane)*cosf(yaw), cosf(tilt + vane)*cosf(yaw))), "all Wraith plumes follow tilt plus pitch and yaw vanes");
      check(close(e.exit[i][3], .28f + .1f*clampf(exhaustPodThrust(p, i), 0, 1)), "Wraith plume radius follows the same iris state as its mesh");
      check(close(e.power[i][2], clampf(p.podThr[i]*1.3f, 0.f, 1.6f)), "per-pod differential thrust scales intensity");
    }
  }
  {
    Plane p = running(kWraith); p.podThr[2] = 0.f; check(build(p).count == 3, "zero-thrust live pod takes no raymarch work");
    p = running(kWraith); p.fail.engineHealth[2] = .45f;
    check(close(exhaustPodThrust(p, 2), .45f), "iris/plume share failure-scaled thrust");
    p.fuel = 0.f; check(exhaustPodThrust(p, 2) == 0.f, "iris/plume share fuel gating");
    check(exhaustPodThrust(p, -1) == 0.f && exhaustPodThrust(p, 4) == 0.f, "pod helper rejects invalid indices");
  }
  printf("XR-30 swept 231 poses; XR-40 swept 99 tilt/vane/yaw poses across all four pods.\n");
  printf("%s: %d exhaust checks, %d failures\n", failures ? "FAIL" : "PASS", checks, failures);
  return failures ? 1 : 0;
}
