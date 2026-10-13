// Phase 1 frame changes, deliberately separate from enabling globe physics.
// The inertial fixture really integrates in the changing local frame. The Plane
// fixture transports a live flight through a rebase, then returns it to canonical
// flat Solace before step(): World, weather, ISA and gravity still use that frame.
#include "../src/geo.h"
#include "../src/aircraft.h"
#include "test_world.h"
#include <exception>

namespace {
const vec3 kAxes[] = {vec3(1, 0, 0), vec3(0, 1, 0), vec3(0, 0, 1)};

struct Maximum {
  double value = 0;
  bool finite = true;
  void add(double v) { finite = finite && std::isfinite(v); value = std::max(value, v); }
  bool below(double limit) const { return finite && value <= limit; }
};

double distance(vec3 a, vec3 b) { return geo::length(geo::Double3(a) - geo::Double3(b)); }
double attitudeError(quat a, quat b) {
  double error = 0;
  for (vec3 axis : kAxes) {
    const double d = distance(a.rotate(axis), b.rotate(axis));
    if (!std::isfinite(d)) return d;
    error = std::max(error, d);
  }
  return error; // unit-axis chord length, approximately radians at these errors
}

bool inertialFlight(const char* name, geo::Position start, vec3 initialVelocity) {
  geo::FloatingOrigin floating(start);
  const geo::LocalFrame initial = floating.frame();
  vec3 p, v = initialVelocity, a(0.0625f, -0.015625f, 0.03125f);
  quat q = quat::axisAngle(vec3(0, 1, 0), 0.37f) * quat::axisAngle(vec3(1, 0, 0), -0.23f)
         * quat::axisAngle(vec3(0, 0, 1), 0.41f);
  q.normalize();
  const quat q0 = q;
  const geo::Double3 p0 = initial.pointToEcef(geo::Double3(p));
  const geo::Double3 v0 = initial.vectorToEcef(geo::Double3(v));
  const geo::Double3 a0 = initial.vectorToEcef(geo::Double3(a));
  Maximum path, speed, acceleration, attitude, jump, velocityJump, accelerationJump, attitudeJump;
  int rebases = 0;
  bool ok = !floating.needsRebase(p);
  // A prescribed constant ECEF acceleration, with no torque. This is an inertial
  // kinematics test, not orbital/gravity simulation. The constant-acceleration
  // update is exact in real arithmetic; the analytic ECEF oracle is never fed
  // back to the local float integrator, even when rebasing.
  const float dt = 1.f;
  for (int step = 1; step <= 400; ++step) {
    p += v * dt + a * (0.5f * dt * dt);
    v += a * dt;
    const double t = double(step) * dt;
    auto observe = [&]() {
      const geo::LocalFrame& frame = floating.frame();
      path.add(geo::length(frame.pointToEcef(geo::Double3(p)) - (p0 + v0 * t + a0 * (0.5 * t * t))));
      speed.add(geo::length(frame.vectorToEcef(geo::Double3(v)) - (v0 + a0 * t)));
      acceleration.add(geo::length(frame.vectorToEcef(geo::Double3(a)) - a0));
      for (vec3 axis : kAxes)
        attitude.add(geo::length(frame.vectorToEcef(geo::Double3(q.rotate(axis)))
                              - initial.vectorToEcef(geo::Double3(q0.rotate(axis)))));
    };
    observe();
    const geo::LocalFrame old = floating.frame();
    const vec3 oldP = p, oldV = v, oldA = a;
    const quat oldQ = q;
    const bool requested = floating.needsRebase(p);
    const auto change = floating.rebase(p);
    ok = ok && bool(change) == requested;
    if (!change) continue;
    ++rebases;
    ok = ok && geo::length(geo::Double3(oldP)) > geo::kRebaseDistance;
    p = change->point(p);
    v = change->vector(v);
    a = change->vector(a); // keeping the old acceleration here changes the path
    q = change->attitude(q);
    const geo::LocalFrame& next = floating.frame();
    jump.add(geo::length(next.pointToEcef(geo::Double3(p)) - old.pointToEcef(geo::Double3(oldP))));
    velocityJump.add(geo::length(next.vectorToEcef(geo::Double3(v)) - old.vectorToEcef(geo::Double3(oldV))));
    accelerationJump.add(geo::length(next.vectorToEcef(geo::Double3(a)) - old.vectorToEcef(geo::Double3(oldA))));
    for (vec3 axis : kAxes)
      attitudeJump.add(geo::length(next.vectorToEcef(geo::Double3(q.rotate(axis)))
                                - old.vectorToEcef(geo::Double3(oldQ.rotate(axis)))));
    ok = ok && geo::length(geo::Double3(p)) < 0.002 && !floating.needsRebase(p);
    observe();
  }
  // The direct rebase is millimetre-accurate. Integrating floats for 400 s has a
  // separate, larger rounding budget; do not call that a millimetre flight test.
  ok = ok && rebases >= 4 && path.below(0.5) && speed.below(0.004)
       && acceleration.below(1e-7) && attitude.below(3e-6)
       && jump.below(0.002) && velocityJump.below(5e-5)
       && accelerationJump.below(1e-8) && attitudeJump.below(1e-6);
  printf("Inertial EUS %-12s: %d rebases, max path %.6f m, velocity %.7f m/s, acceleration %.3g m/s^2, attitude %.3g; "
         "rebase jumps %.3g m / %.3g m/s / %.3g m/s^2 / %.3g %s\n",
         name, rebases, path.value, speed.value, acceleration.value, attitude.value,
         jump.value, velocityJump.value, accelerationJump.value, attitudeJump.value, ok ? "ok" : "FAIL");
  return ok;
}

// Only a test adapter, not a production Plane rebase method. All physics steps
// remain canonical. Scalar heading/altitude targets therefore stay canonical,
// and the private gust cache is overwritten by substep before its next use.
void transportPlane(Plane& p, const geo::FrameChange& change) {
  p.pos = change.point(p.pos);
  p.wxAir = change.point(p.wxAir); // pos - wxAir locates the advected weather
  p.apTd = change.point(p.apTd);
  p.apHoldC = change.point(p.apHoldC);
  p.vel = change.vector(p.vel);
  p.apVelPrev = change.vector(p.apVelPrev);
  p.windVel = change.vector(p.windVel);
  p.windAvg = change.vector(p.windAvg);
  p.wxl.draft = change.vector(p.wxl.draft);
  p.seamShift = change.vector(p.seamShift);
  p.apLd = change.vector(p.apLd);
  p.apStuntRight0 = change.vector(p.apStuntRight0);
  p.q = change.attitude(p.q);
  // w, gustRot, ctlSurf, surf and AeroWake are body-space. Do not rotate them.
}

bool sameBodyState(const Plane& a, const Plane& b) {
  if (distance(a.w, b.w) != 0 || distance(a.gustRot, b.gustRot) != 0
      || distance(a.ctlSurf, b.ctlSurf) != 0 || distance(a.surf, b.surf) != 0
      || distance(a.wake.aft, b.wake.aft) != 0 || a.wake.nv != b.wake.nv || a.wake.nj != b.wake.nj)
    return false;
  for (int i = 0; i < a.wake.nv; ++i) if (distance(a.wake.v[i].r, b.wake.v[i].r) != 0) return false;
  for (int i = 0; i < a.wake.nj; ++i) if (distance(a.wake.j[i].r, b.wake.j[i].r) != 0) return false;
  return true;
}

struct TransportErrors {
  Maximum points, vectors, attitude;
  bool body = true;
  void inspect(const Plane& moved, const geo::LocalFrame& frame,
               const Plane& original, const geo::LocalFrame& canonical) {
    const vec3 movedPoints[] = {moved.pos, moved.wxAir, moved.apTd, moved.apHoldC};
    const vec3 originalPoints[] = {original.pos, original.wxAir, original.apTd, original.apHoldC};
    for (int i = 0; i < 4; ++i)
      points.add(geo::length(frame.pointToEcef(geo::Double3(movedPoints[i]))
                          - canonical.pointToEcef(geo::Double3(originalPoints[i]))));
    const vec3 movedVectors[] = {moved.vel, moved.apVelPrev, moved.windVel, moved.windAvg,
                                moved.wxl.draft, moved.seamShift, moved.apLd, moved.apStuntRight0,
                                moved.pos - moved.wxAir};
    const vec3 originalVectors[] = {original.vel, original.apVelPrev, original.windVel, original.windAvg,
                                   original.wxl.draft, original.seamShift, original.apLd, original.apStuntRight0,
                                   original.pos - original.wxAir};
    for (int i = 0; i < 9; ++i)
      vectors.add(geo::length(frame.vectorToEcef(geo::Double3(movedVectors[i]))
                           - canonical.vectorToEcef(geo::Double3(originalVectors[i]))));
    for (vec3 axis : kAxes)
      attitude.add(geo::length(frame.vectorToEcef(geo::Double3(moved.q.rotate(axis)))
                            - canonical.vectorToEcef(geo::Double3(original.q.rotate(axis)))));
    body = body && sameBodyState(moved, original);
  }
};

bool planeFlight(bool turbulent) {
  Weather wx;
  wx.windSpeed = turbulent ? 7.f : 0.f;
  wx.windFrom = 250;
  wx.gust = turbulent ? 3.f : 0.f;
  wx.turbulence = turbulent ? 0.3f : 0.f;
  wx.cloudCover = 0;
  const AircraftSpec& spec = kAircraft[1];
  Plane reference;
  reference.reset(&spec, vec3(19900, 1500, 200), 90, spec.maxFuel * 0.5f, 85, true, spec.cruise * 0.9f);
  reference.ctl.throttle = 0.7f;
  // Nonzero, distinct unused navigation caches make point/vector mistakes visible
  // at the intermediate frame, even with the autopilot switched off in this flight.
  reference.apTd = vec3(17300, 40, -1700);
  reference.apHoldC = vec3(18300, 0, 1200);
  reference.apLd = normalize(vec3(1, 0, -2));
  reference.apStuntRight0 = reference.right();
  reference.apVelPrev = reference.vel;
  Plane rebased = reference;
  const geo::LocalFrame canonical(geo::solaceOrigin());
  geo::FloatingOrigin floating(geo::solaceOrigin());
  TransportErrors transport;
  Maximum path, speed, attitude, bodyRate, wind, gLoad, ias, density, fuel;
  int rebases = 0, stepsAfterRebase = 0;
  double crossing = -1;
  bool ok = !floating.needsRebase(reference.pos);
  const float dt = 1.f / 60.f;
  for (int step = 0; step < 30 * 60; ++step) {
    const float t = step * dt;
    reference.step(dt, wx, t);
    rebased.step(dt, wx, t);
    if (rebases) ++stepsAfterRebase;
    // These are AFFINE tangent coordinates. The nonlinear azimuthal-equidistant
    // fromSolace()/toSolace() chart would distort a flat Plane trajectory here.
    const geo::LocalFrame old = floating.frame();
    const geo::FrameChange canonicalToOld(canonical, old);
    const vec3 focus = canonicalToOld.point(rebased.pos);
    const bool requested = floating.needsRebase(focus);
    const auto change = floating.rebase(focus);
    ok = ok && bool(change) == requested;
    if (change) {
      ++rebases;
      if (crossing < 0) crossing = (step + 1) * double(dt);
      const Plane before = rebased;
      transportPlane(rebased, canonicalToOld);
      transport.inspect(rebased, old, before, canonical);
      transportPlane(rebased, *change);
      transport.inspect(rebased, floating.frame(), before, canonical);
      ok = ok && geo::length(geo::Double3(rebased.pos)) < 0.002;
      transportPlane(rebased, geo::FrameChange(floating.frame(), canonical));
      transport.inspect(rebased, canonical, before, canonical);
    }
    path.add(distance(reference.pos, rebased.pos));
    speed.add(distance(reference.vel, rebased.vel));
    attitude.add(attitudeError(reference.q, rebased.q));
    bodyRate.add(distance(reference.w, rebased.w));
    wind.add(distance(reference.windVel, rebased.windVel));
    wind.add(distance(reference.windAvg, rebased.windAvg));
    gLoad.add(std::abs(double(reference.gLoad) - rebased.gLoad));
    ias.add(std::abs(double(reference.ias) - rebased.ias));
    density.add(std::abs(double(reference.density) - rebased.density));
    fuel.add(std::abs(double(reference.fuel) - rebased.fuel));
    ok = ok && !reference.ev.crashed && !rebased.ev.crashed && !reference.onGround && !rebased.onGround;
    if (!ok) break;
  }
  // Float positions around x=20 km have ~2 mm spacing. Subsequent aerodynamic
  // integration and spatial weather can amplify the initial rounding slightly.
  // Check the whole continuation, not just the instant or endpoint of a rebase.
  ok = ok && rebases == 1 && crossing > 0 && crossing < 5 && stepsAfterRebase > 20 * 60
       && path.below(0.05) && speed.below(0.003) && attitude.below(1e-4)
       && bodyRate.below(1e-4) && wind.below(0.003) && gLoad.below(0.001)
       && ias.below(0.003) && density.below(1e-5) && fuel.below(1e-4)
       && transport.points.below(0.003) && transport.vectors.below(0.004)
       && transport.attitude.below(1e-6) && transport.body
       && distance(reference.seamShift, vec3()) == 0 && distance(rebased.seamShift, vec3()) == 0;
  printf("Plane canonical %-9s: %d rebase at %.3f s, continued %.2f s; max path %.6f m, velocity %.7f m/s, "
         "attitude %.3g, body rate %.3g, wind %.3g m/s, g %.3g, IAS %.3g m/s, density %.3g, fuel %.3g kg; "
         "transport %.6f m / %.6f vector / %.3g attitude, body unchanged %s %s\n",
         turbulent ? "turbulent" : "calm", rebases, crossing, stepsAfterRebase * double(dt),
         path.value, speed.value, attitude.value, bodyRate.value, wind.value, gLoad.value, ias.value, density.value, fuel.value,
         transport.points.value, transport.vectors.value, transport.attitude.value,
         transport.body ? "yes" : "NO", ok ? "ok" : "FAIL");
  return ok;
}
} // namespace

int main() {
  try {
    int failures = 0;
    failures += !inertialFlight("Solace", {32.5, -58, 7500}, vec3(220, 6, -130));
    failures += !inertialFlight("date line", {15, 179.8, 7500}, vec3(260, 6, -70));
    failures += !inertialFlight("near pole", {89.8, 40, 7500}, vec3(30, 6, -250));
    buildTestWorld();
    failures += !planeFlight(false);
    failures += !planeFlight(true);
    printf("Geographic flight invariance: %d failures\n", failures);
    return failures ? 1 : 0;
  } catch (const std::exception& error) {
    printf("Geographic flight invariance: unexpected exception: %s\n", error.what());
    return 1;
  }
}
