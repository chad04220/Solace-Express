// Surface-chart regional flight regression. This tests actual local Plane steps
// and geographic transport, not ECEF/orbital dynamics or background generation.
#include "../src/region_travel.h"
#ifndef REGION_TRAVEL_MATH_ONLY
#include "../src/aircraft.h"
#endif
#include <limits>
#include <stdexcept>

namespace {
int checks = 0, failures = 0;
void expect(bool pass, const char* label) {
  ++checks;
  if (!pass) { if (failures < 30) printf("FAIL: %s\n", label); ++failures; }
}
bool finite(double x) {
  uint64_t bits; std::memcpy(&bits, &x, sizeof(bits));
  return (bits & 0x7ff0000000000000ull) != 0x7ff0000000000000ull;
}
bool near(double a, double b, double tolerance) {
  return finite(a) && finite(b) && std::abs(a - b) <= tolerance;
}
double dist(vec3 a, vec3 b = {}) { return geo::length(geo::Double3(a) - geo::Double3(b)); }
double angleError(double a, double b) { return std::abs(std::remainder(a - b, 360.)); }
double heading(vec3 v) { return std::atan2(double(v.x), -double(v.z)) / geo::kDegrees; }
template<class F> void rejects(F f, const char* label) {
  bool rejected = false;
  try { f(); } catch (const std::invalid_argument&) { rejected = true; }
  catch (const std::overflow_error&) { rejected = true; }
  expect(rejected, label);
}
const vec3 axes[] = {{1, 0, 0}, {0, 1, 0}, {0, 0, -1}};
double attitudeError(quat a, quat b) {
  double result = 0;
  for (auto axis : axes) {
    const double e = dist(a.rotate(axis), b.rotate(axis));
    if (!finite(e)) return std::numeric_limits<double>::infinity();
    result = std::max(result, e);
  }
  return result;
}

// Independent ECEF great-circle oracle, avoiding the destination()/bearing()
// implementation used by Chart. The polar longitude follows Chart::origin().
geo::Position radialOracle(geo::Position origin, vec3 local) {
  const double r = std::hypot(double(local.x), double(local.z));
  const geo::LocalFrame basis(origin);
  const auto outward = geo::direction(origin);
  const auto tangent = r ? basis.vectorToEcef({local.x / r, 0, local.z / r}) : geo::Double3();
  return geo::fromDirection(outward * std::cos(r / geo::radius()) + tangent * std::sin(r / geo::radius()), local.y);
}
geo::Double3 physicalTangent(const regionTravel::Chart& chart, vec3 p, vec3 direction) {
  direction.y = 0;
  direction = normalize(direction);
  // A sub-metre step would amplify 4 mm float spacing at chart boundaries.
  // Symmetric 1 km chords suppress it while curvature bias stays negligible.
  const auto a = geo::direction(chart.position(p - direction * 1024.f));
  const auto b = geo::direction(chart.position(p + direction * 1024.f));
  return geo::unit(b - a);
}

void chartTests() {
  using namespace geo;
  std::vector<Position> origins = {solaceOrigin(), {0, 179.99, 0}, {0, -179.99, 0},
    {89.999999, 123, 0}, {-89.999999, -33, 0}, {90, 0, 0}, {-90, 0, 0}};
  // Every cube face, edge and corner, including exact tie boundaries. The chart
  // must not inherit a face change or the longitude discontinuity as a teleport.
  for (int face = 0; face < 6; ++face)
    for (double u : {-1., 0., 1.}) for (double v : {-1., 0., 1.})
      origins.push_back(fromDirection(faceDirection(Face(face), u, v)));
  Rng rng(718191);
  double worstRoundtrip = 0, worstOracle = 0, worstGeographic = 0;
  for (auto origin : origins) {
    const regionTravel::Chart chart(origin);
    expect(chart.origin().height == 0, "regional origin is on reference surface");
    expect(dist(chart.local(chart.origin())) == 0, "chart centre is exact local zero");
    for (int i = 0; i < 100; ++i) {
      const vec3 p(rng.range(-60000, 60000), rng.range(-2000, 90000), rng.range(-60000, 60000));
      const auto geographic = chart.position(p);
      const auto oracle = radialOracle(chart.origin(), p);
      const double roundtrip = dist(chart.local(geographic), p);
      const double oracleError = surfaceDistance(oracle, geographic);
      worstRoundtrip = std::max(worstRoundtrip, roundtrip);
      worstOracle = std::max(worstOracle, oracleError);
      expect(roundtrip < .012, "global chart float roundtrip below 1.2 cm");
      expect(oracleError < 2e-6, "chart agrees with independent ECEF great-circle oracle");
      expect(geographic.height == p.y, "chart y is altitude with no curvature sag");
      expect(near(surfaceDistance(chart.origin(), geographic), std::hypot(double(p.x), double(p.z)), 2e-6),
        "chart radial metres are true surface metres");
      auto fine = destination(geographic, 131.27, .013579);
      const auto restored = chart.position(chart.local(fine));
      const double error = surfaceDistance(fine, restored);
      worstGeographic = std::max(worstGeographic, error);
      expect(error < .012, "double geographic location survives local float narrowing");
    }
  }
  const regionTravel::Chart solace;
  for (float x : {-50000.f, -40000.f, 0.f, 40000.f, 50000.f})
    for (float z : {-50000.f, -40000.f, 0.f, 40000.f, 50000.f}) {
      const vec3 p(x, 731.25f, z);
      expect(surfaceDistance(solace.position(p), fromSolace(p)) < 1e-7, "authored chart matches prior Solace projection");
    }
  const regionTravel::Chart dateLine({0, 179.99, 0});
  const auto seamA = dateLine.position({1000, 42, 0}), seamB = dateLine.position({1200, 42, 0});
  expect(seamA.longitude > 0 && seamB.longitude < 0, "date-line fixture actually crosses longitude seam");
  expect(near(surfaceDistance(seamA, seamB), 200., 1e-6), "longitude seam is physically continuous");
  printf("Chart: roundtrip %.9g m, independent oracle %.9g m, geographic narrowing %.9g m\n",
    worstRoundtrip, worstOracle, worstGeographic);
}

void transportTests() {
  using namespace geo;
  double worstJump = 0, worstDirection = 0, worstSpeed = 0, worstAttitude = 0;
  int nonzeroConvergence = 0;
  const Position origins[] = {solaceOrigin(), {0, 179.9, 0}, {20, -179.9, 0},
    {89.9, 50, 0}, {-89.9, -120, 0}, {90, 0, 0}, {-90, 0, 0},
    {0, 45, 0}, {35.264389682754654, 45, 0}, {-35.264389682754654, -135, 0}};
  for (auto origin : origins) for (vec3 focus : {vec3(), vec3(35000, 7200, -32000), vec3(-45000, -3, 18000)}) {
    const regionTravel::Chart from(origin);
    const auto at = from.position(focus);
    for (double bearing : {0., 73., 180., 271.}) {
      const regionTravel::Chart to(destination(at, bearing, 23000));
      const regionTravel::Change change(from, to, focus);
      const vec3 moved = change.point(focus);
      const regionTravel::Change inverse(to, from, moved);
      if (std::abs(change.headingDelta()) > 1) ++nonzeroConvergence;
      expect(moved.y == focus.y, "transition preserves altitude exactly");
      expect(dist(inverse.point(moved), focus) < .015, "regional position roundtrip");
      const double jump = surfaceDistance(at, to.position(moved));
      worstJump = std::max(worstJump, jump);
      expect(jump < .01, "region change keeps true globe position");
      expect(dist(change.vector({0, 1, 0}), {0, 1, 0}) == 0, "chart up remains exact altitude axis");
      for (vec3 v : {vec3(210, 7, -98), vec3(-120, -21, 300), vec3(0, 100, 0), vec3()}) {
        const auto movedV = change.vector(v);
        const double speedError = std::abs(dist(movedV) - dist(v));
        worstSpeed = std::max(worstSpeed, speedError);
        expect(speedError < .00012, "horizontal transport preserves physical speed");
        expect(movedV.y == v.y, "vertical speed is not curved into horizontal velocity");
        expect(dist(inverse.vector(movedV), v) < .0004, "vector transport roundtrip");
      }
      for (float h : {0.f, 45.f, 90.f, 135.f, 180.f, 225.f, 270.f, 359.9f, -720.f}) {
        const vec3 v(float(std::sin(h * kDegrees)), 0, float(-std::cos(h * kDegrees)));
        const auto movedV = change.vector(v);
        expect(angleError(heading(movedV), change.heading(h)) < .00005, "heading and vector rotations agree in sign and amount");
        const auto before = physicalTangent(from, focus, v);
        const auto after = physicalTangent(to, moved, movedV);
        const double e = length(before - after);
        worstDirection = std::max(worstDirection, e);
        // AE charts have tiny differential anisotropy: this rotation-only local
        // physics bridge is not an exact general-purpose chart Jacobian.
        expect(e < .00006, "surface travel direction remains continuous across local charts");
        expect(change.heading(h) >= 0 && change.heading(h) < 360, "heading stays canonical");
      }
      const quat q = quat::axisAngle({0, 1, 0}, 1.2f) * quat::axisAngle({1, 0, 0}, -.33f)
                   * quat::axisAngle({0, 0, 1}, .49f);
      const auto q2 = change.attitude(q);
      for (auto axis : axes) {
        const double e = dist(q2.rotate(axis), change.vector(q.rotate(axis)));
        worstAttitude = std::max(worstAttitude, e);
        expect(e < 8e-7, "attitude carries all body axes through same chart rotation");
      }
      expect(attitudeError(inverse.attitude(q2), q) < 2e-6, "attitude roundtrip");
      for (float scale : {1e-30f, 1e30f, -1e30f}) {
        const quat scaled(q.w * scale, q.x * scale, q.y * scale, q.z * scale);
        expect(attitudeError(change.attitude(scaled), q2) < 1e-6, "finite nonzero quaternion normalization is scale invariant");
      }
      // True surface points, unlike vectors, must be nonlinearly re-expressed.
      for (vec3 p : {focus + vec3(7000, -300, 3000), focus + vec3(-5000, 800, -9000)}) {
        const auto p2 = change.point(p);
        expect(surfaceDistance(from.position(p), to.position(p2)) < .015, "nearby camera/navigation points keep geography");
        expect(p2.y == p.y, "nearby points keep individual altitudes");
      }
      const auto target = destination(at, 47, 2500000);
      const auto h1 = from.bearing(focus, target), h2 = to.bearing(moved, target);
      expect(h1 && h2 && angleError(change.heading(float(*h1)), *h2) < .002,
        "distant destination bearing crosses chart and date-line boundaries without reversal");
    }
  }
  expect(nonzeroConvergence > 20, "fixtures exercise nontrivial polar meridian convergence");
  printf("Transport: position jump %.9g m, direction chord %.9g, speed %.9g m/s, attitude axis %.9g\n",
    worstJump, worstDirection, worstSpeed, worstAttitude);
}

void planarRotationTests() {
  double worstSpeed = 0, worstOracle = 0;
  // Polar meridian convergence sweeps the complete rotation range, including
  // near-half-turns where float quaternion rotation accumulates scale error.
  for (double pole : {-90., 90.}) {
    const regionTravel::Chart from({pole, 0, 0});
    for (int step = -720; step <= 720; ++step) {
      const regionTravel::Chart to({pole > 0 ? 89.9 : -89.9, step * .25, 0});
      const regionTravel::Change change(from, to, {});
      for (vec3 v : {vec3(210, 7, -98), vec3(-120, -21, 300), vec3(0, 100, 0), vec3()}) {
        const vec3 moved = change.vector(v);
        // Independent polar-coordinate oracle: preserve horizontal magnitude
        // and add heading, rather than reuse a quaternion or matrix product.
        const double radius = std::hypot(double(v.x), double(v.z));
        const double angle = std::atan2(double(v.x), -double(v.z)) + change.headingDelta() * geo::kDegrees;
        const geo::Double3 oracle(radius * std::sin(angle), v.y, -radius * std::cos(angle));
        const double oracleError = geo::length(geo::Double3(moved) - oracle);
        const double speedError = std::abs(dist(moved) - dist(v));
        worstOracle = std::max(worstOracle, oracleError);
        worstSpeed = std::max(worstSpeed, speedError);
        expect(oracleError < .000025, "planar transport matches independent polar oracle within float narrowing budget");
        expect(speedError < .00012, "full-angle transport preserves physical speed");
        expect(moved.y == v.y, "full-angle transport preserves vertical speed exactly");
      }
    }
  }
  printf("Planar rotation sweep: speed %.9g m/s, independent polar oracle %.9g m/s\n", worstSpeed, worstOracle);
}

void ownershipBoundaryTests() {
  using namespace geo;
  double maxRadius = 0, maxJump = 0, maxDirection = 0;
  int faceCrossings = 0, poleCrossings = 0, dateLineCrossings = 0;
  const double edges[][2] = {{0, .5}, {1, .5}, {.5, 0}, {.5, 1}, {0, 0}, {1, 1}};
  // The runtime uses level-5 ownership. Its regional centres are hundreds of
  // kilometres apart, although each island's generated land covers only 76 km.
  for (int face = 0; face < 6; ++face) for (int x : {0, 15, 16, 31}) for (int y : {0, 15, 16, 31}) {
    const Tile oldTile{Face(face), 5, x, y};
    const Double3 centre = tileDirection(oldTile, .5, .5);
    const regionTravel::Chart from(fromDirection(centre));
    for (const auto& edge : edges) {
      const Double3 boundary = tileDirection(oldTile, edge[0], edge[1]);
      const Double3 tangent = unit(cross(unit(cross(centre, boundary)), boundary));
      // 300 m past a true ownership edge, independently on its great circle.
      const double angle = 300. / radius();
      const Position at = fromDirection(boundary * std::cos(angle) + tangent * std::sin(angle), 5231.25);
      const Tile nextTile = positionTile(at, 5).tile;
      expect(nextTile.face != oldTile.face || nextTile.x != x || nextTile.y != y,
        "ownership-boundary fixture actually enters another cell");
      faceCrossings += nextTile.face != oldTile.face;
      poleCrossings += std::abs(fromDirection(boundary).latitude) > 89.999;
      const regionTravel::Chart to(fromDirection(tileDirection(nextTile, .5, .5)));
      dateLineCrossings += std::abs(from.origin().longitude - to.origin().longitude) > 180;
      const vec3 focus = from.local(at);
      const regionTravel::Change change(from, to, focus);
      const vec3 moved = change.point(focus);
      maxRadius = std::max(maxRadius, std::hypot(double(focus.x), double(focus.z)));
      const double jump = surfaceDistance(from.position(focus), to.position(moved));
      maxJump = std::max(maxJump, jump);
      expect(jump < .025 && moved.y == focus.y, "actual ownership-edge transport retains geography and altitude");
      const regionTravel::Change inverse(to, from, moved);
      expect(dist(inverse.point(moved), focus) < .035, "actual ownership-edge geographic roundtrip");
      for (double heading : {0., 45., 90., 180., 270.}) {
        const vec3 v(float(std::sin(heading * kDegrees)), 0, float(-std::cos(heading * kDegrees)));
        const double error = length(physicalTangent(from, focus, v) - physicalTangent(to, moved, change.vector(v)));
        maxDirection = std::max(maxDirection, error);
        // Rotation intentionally preserves speed rather than importing an AE
        // chart's anisotropic scale into the aerodynamic physics. Bound and
        // report its small directional approximation at real cell boundaries.
        expect(error < .0007, "actual ownership-edge direction approximation stays below 0.041 degrees");
      }
    }
  }
  expect(faceCrossings >= 100 && poleCrossings >= 4 && dateLineCrossings >= 8,
    "actual ownership tests cross cube faces, poles and date line");
  printf("Ownership edges: %d face / %d pole / %d date-line crossings, max chart radius %.3f km, jump %.9g m, direction chord %.9g\n",
    faceCrossings, poleCrossings, dateLineCrossings, maxRadius / 1000, maxJump, maxDirection);
}

void invalidInputTests() {
  using namespace regionTravel;
  const Chart chart({0, 0, 0});
  const Change change(chart, Chart({1, 1, 0}), {});
  for (float bad : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
                    -std::numeric_limits<float>::infinity()}) {
    rejects([&] { chart.position({bad, 0, 0}); }, "invalid chart x rejected under fast math");
    rejects([&] { chart.position({0, bad, 0}); }, "invalid chart altitude rejected under fast math");
    rejects([&] { chart.position({0, 0, bad}); }, "invalid chart z rejected under fast math");
    rejects([&] { chart.local({bad, 0, 0}); }, "invalid geographic latitude rejected");
    rejects([&] { chart.local({0, bad, 0}); }, "invalid geographic longitude rejected");
    rejects([&] { chart.local({0, 0, bad}); }, "invalid geographic altitude rejected");
    rejects([&] { change.vector({bad, 0, 0}); }, "invalid velocity rejected under fast math");
    rejects([&] { change.attitude({1, 0, bad, 0}); }, "invalid attitude rejected under fast math");
    rejects([&] { change.heading(bad); }, "invalid heading rejected under fast math");
    rejects([&] { Change invalid(chart, chart, {0, bad, 0}); }, "invalid change focus rejected");
  }
  rejects([&] { chart.local({0, 180, 0}); }, "chart inverse rejects exact antipode");
  rejects([&] { chart.position({float(geo::radius() * geo::kPi + 10), 0, 0}); }, "chart forward rejects beyond antipode");
  rejects([&] { chart.position({0, float(-geo::radius()), 0}); }, "chart rejects altitude at planet centre");
  rejects([&] { change.attitude({0, 0, 0, 0}); }, "zero attitude rejected");
  expect(!chart.bearing({}, {0, 0, 100}), "coincident route bearing is undefined");
  expect(!chart.bearing({}, {0, 180, 0}), "antipodal route bearing is undefined");
}

#ifndef REGION_TRAVEL_MATH_ONLY
void seaWorld() {
  g_world = World();
  g_world.hm.assign(size_t(HM_N) * HM_N * 4, 0);
  for (size_t i = 0; i < g_world.hm.size(); i += 4) g_world.hm[i] = -SEA_DEPTH;
  g_world.repeating = false;
  g_world.procedural = true;
  g_groundPits.clear();
}
bool sameBodyState(const Plane& a, const Plane& b) {
  if (dist(a.w, b.w) || dist(a.gustRot, b.gustRot) || dist(a.ctlSurf, b.ctlSurf) || dist(a.surf, b.surf)
      || dist(a.wake.aft, b.wake.aft) || a.wake.nv != b.wake.nv || a.wake.nj != b.wake.nj) return false;
  for (int i = 0; i < a.wake.nv; ++i) if (dist(a.wake.v[i].r, b.wake.v[i].r)) return false;
  for (int i = 0; i < a.wake.nj; ++i) if (dist(a.wake.j[i].r, b.wake.j[i].r)) return false;
  return true;
}
// This adapter exercises helper mathematics during actual Plane stepping. Game's
// production transport, loading pause and career isolation have their own runtime
// fixture; this adapter cannot substitute for those integration checks.
void transport(Plane& p, const regionTravel::Change& change) {
  const vec3 airRelative = p.pos - p.wxAir;
  p.pos = change.point(p.pos); p.wxAir = p.pos - change.vector(airRelative);
  p.apTd = change.point(p.apTd); p.apHoldC = change.point(p.apHoldC);
  p.vel = change.vector(p.vel); p.apVelPrev = change.vector(p.apVelPrev);
  p.windVel = change.vector(p.windVel); p.windAvg = change.vector(p.windAvg);
  p.wxl.draft = change.vector(p.wxl.draft); p.seamShift = change.vector(p.seamShift);
  p.apLd = change.vector(p.apLd); p.apStuntRight0 = change.vector(p.apStuntRight0);
  p.q = change.attitude(p.q);
  p.apHeading = change.heading(p.apHeading); p.apStuntHdg = change.heading(p.apStuntHdg);
}
void flightTests() {
  seaWorld();
  World authored;
  expect(!authored.repeating && authored.wrap(50001) == 50001, "the authored islands no longer repeat: one place on the Earth");
  expect(g_world.wrap(50001) == 50001 && g_world.wrap(-50001) == -50001, "procedural world disables both wrap directions");
  expect(authored.nearPoint(-49000, 49000) == -49000 && g_world.nearPoint(-49000, 49000) == -49000,
    "navigation never chooses a repeated copy, over the islands or a generated region");
  Weather wx; wx.windSpeed = wx.gust = wx.turbulence = wx.cloudCover = 0; wx.timeOfDay = 0;
  // (the Emissary, at a jet's speed: the Ambler flew this at 180 m/s, nearly three times its cruise, until its structure
  // became a normal-category +3.8 g one and failed there at once)
  const AircraftSpec& spec = kAircraft[6];
  const float dt = 1.f / 60.f;
  for (float h : {0.f, 90.f, 180.f, 270.f}) {
    const vec3 direction(float(std::sin(h * geo::kDegrees)), 0, float(-std::cos(h * geo::kDegrees)));
    Plane p; p.reset(&spec, direction * 49995 + vec3(0, 4000, 0), h, spec.maxFuel * .7f, 85, true, 180);
    p.sceneryHits = false;
    const float fuel = p.fuel;
    const vec3 start = p.pos;
    for (int step = 0; step < 120; ++step) {
      const vec3 before = p.pos;
      p.step(dt, wx, step * dt);
      expect(dist(p.pos, before) < 10, "live Plane crossing never jumps by torus period");
      expect(dist(p.seamShift) == 0, "nonrepeating Plane emits no authored seam shift");
      expect(!p.ev.crashed && !p.onGround, "boundary crossing remains live airborne flight");
    }
    expect(dot(p.pos, direction) > 50100, "live Plane actually continues beyond 50 km boundary");
    expect(dist(p.pos, start) > 100, "crossing fixture advances actual physics");
    expect(near(p.flightTime, 2, .00001), "boundary crossing keeps accumulating elapsed flight time");
    expect(p.fuel < fuel && fuel - p.fuel < 1, "crossing burns normal fuel without refuel/reset");
  }
  // Deliberately high-latitude/date-line origin forces appreciable heading
  // transport while both runs share calm sea and identical local aerodynamics.
  const regionTravel::Chart from({89.7, 179.9, 0});
  Plane reference; reference.reset(&spec, {49995, 4000, -23000}, 90, spec.maxFuel * .7f, 85, true, 180);
  reference.sceneryHits = false;
  for (int step = 0; step < 30; ++step) reference.step(dt, wx, step * dt);
  expect(reference.pos.x > 50000, "live transport occurs after actual nonwrapping boundary crossing");
  Plane moved = reference;
  const regionTravel::Chart to(from.position(reference.pos));
  const regionTravel::Change change(from, to, reference.pos);
  transport(moved, change);
  expect(geo::surfaceDistance(from.position(reference.pos), to.position(moved.pos)) < .002, "live transfer does not teleport on globe");
  expect(moved.pos.y == reference.pos.y && moved.fuel == reference.fuel && moved.flightTime == reference.flightTime &&
         moved.payload == reference.payload && moved.engineSpool == reference.engineSpool && moved.rng.s == reference.rng.s,
    "live transfer retains altitude/fuel/time/payload/engine/random state");
  expect(sameBodyState(moved, reference), "live transfer leaves angular rates/control surfaces/wake in body space");
  expect(dist(moved.pos - moved.wxAir, change.vector(reference.pos - reference.wxAir)) < .005,
    "live transfer preserves the aircraft-relative advected weather coordinate");
  double maxPath = 0, maxFuel = 0, maxSpeed = 0;
  for (int step = 30; step < 150; ++step) {
    reference.step(dt, wx, step * dt); moved.step(dt, wx, step * dt);
    maxPath = std::max(maxPath, geo::surfaceDistance(from.position(reference.pos), to.position(moved.pos)));
    maxSpeed = std::max(maxSpeed, std::abs(dist(reference.vel) - dist(moved.vel)));
    maxFuel = std::max(maxFuel, std::abs(double(reference.fuel) - moved.fuel));
    expect(!reference.ev.crashed && !moved.ev.crashed, "both live flights continue after geographic transfer");
    expect(reference.flightTime == moved.flightTime, "post-transfer physics preserves elapsed-time continuity");
  }
  expect(maxPath < .08, "post-transfer live flight maintains geographic path within local float budget");
  expect(maxSpeed < .003 && maxFuel < .0001, "post-transfer flight preserves speed and fuel flow");
  printf("Live chart flight: continued 2 s, maximum path %.9g m, speed %.9g m/s, fuel %.9g kg\n", maxPath, maxSpeed, maxFuel);
}
#endif
} // namespace

int main() {
  try {
    chartTests(); transportTests(); planarRotationTests(); ownershipBoundaryTests(); invalidInputTests();
#ifndef REGION_TRAVEL_MATH_ONLY
    flightTests();
#endif
  } catch (const std::exception& e) { printf("Unexpected exception: %s\n", e.what()); ++failures; }
  printf("Regional surface travel: %d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
