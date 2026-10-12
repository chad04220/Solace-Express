#include "region_travel.h"
#include <cstring>
#include <stdexcept>

namespace regionTravel {
namespace {
// Preserve invalid-input checks under the game's /fp:fast and -ffast-math builds.
bool finite(double value) { uint64_t bits; std::memcpy(&bits, &value, sizeof(bits)); return (bits & 0x7ff0000000000000ull) != 0x7ff0000000000000ull; }
void require(bool yes, const char* why) { if (!yes) throw std::invalid_argument(why); }
double chartHeading(geo::Double3 v) { return std::atan2(v.x, -v.z) / geo::kDegrees; }
double wrapped(double degrees) { const double v = degrees - 360. * std::floor(degrees / 360.); return v >= 360. ? 0. : v; }
}
Chart::Chart(geo::Position origin) : origin_(geo::fromDirection(geo::direction(origin), 0)) {}
geo::Position Chart::position(vec3 p) const {
  require(finite(p.x) && finite(p.y) && finite(p.z), "non-finite regional position");
  const double distance = std::hypot(double(p.x), double(p.z));
  require(distance < geo::radius() * (geo::kPi - 1e-12), "regional chart reaches its antipode");
  geo::Position result = distance > 0 ? geo::destination(origin_, std::atan2(double(p.x), -double(p.z)) / geo::kDegrees, distance) : origin_;
  result.height = p.y;
  (void)geo::direction(result); // validates altitude too
  return result;
}
geo::Double3 Chart::localDouble(geo::Position p) const {
  const double distance = geo::surfaceDistance(origin_, p);
  require(distance < geo::radius() * (geo::kPi - 1e-12), "regional chart reaches its antipode");
  if (distance < 1e-7) return {0, p.height, 0};
  const auto bearing = geo::initialBearing(origin_, p);
  require(bearing.has_value(), "ambiguous regional chart bearing");
  const double angle = *bearing * geo::kDegrees;
  return {std::sin(angle) * distance, p.height, -std::cos(angle) * distance};
}
vec3 Chart::local(geo::Position p) const { return geo::toFloat(localDouble(p)); }
std::optional<double> Chart::bearing(vec3 focus, geo::Position target) const {
  const geo::Position at = position(focus);
  const auto trueBearing = geo::initialBearing(at, target);
  if (!trueBearing) return std::nullopt;
  // A short great-circle tangent expressed in this surface chart, avoiding a
  // longitude seam and the singularity of subtracting distant chart positions.
  const geo::Position ahead = geo::destination(at, *trueBearing, 100.);
  const auto direction = localDouble(ahead) - localDouble(at);
  return wrapped(chartHeading(direction));
}
Change::Change(const Chart& from, const Chart& to, vec3 focus) : from_(from), to_(to) {
  const geo::Position at = from.position(focus);
  // A common surface tangent carries the aircraft's heading between charts.
  // At the geographic pole choose its eastward tangent; north is arbitrary there.
  const double azimuth = std::abs(at.latitude) > 89.999999 ? 90. : 0.;
  const geo::Position ahead = geo::destination(at, azimuth, 100.);
  const auto oldTangent = from.localDouble(ahead) - from.localDouble(at);
  const auto newTangent = to.localDouble(ahead) - to.localDouble(at);
  headingDelta_ = std::remainder(chartHeading(newTangent) - chartHeading(oldTangent), 360.);
  const double angle = headingDelta_ * geo::kDegrees;
  horizontalCos_ = std::cos(angle);
  horizontalSin_ = std::sin(angle);
  // Positive quaternion Y rotates -z west; chart headings increase eastward.
  rotation_ = quat::axisAngle(vec3(0, 1, 0), float(-angle));
}
vec3 Change::point(vec3 local) const { return to_.local(from_.position(local)); }
vec3 Change::vector(vec3 local) const {
  require(finite(local.x) && finite(local.y) && finite(local.z), "non-finite regional vector");
  // A float quaternion is only approximately unit length; its cross products
  // can add speed drift under fast math. Rotate horizontally in double and
  // narrow just once, retaining the altitude-axis component exactly.
  return {float(horizontalCos_ * double(local.x) - horizontalSin_ * double(local.z)), local.y,
          float(horizontalSin_ * double(local.x) + horizontalCos_ * double(local.z))};
}
quat Change::attitude(quat q) const {
  require(finite(q.w) && finite(q.x) && finite(q.y) && finite(q.z), "non-finite regional attitude");
  const double n = std::hypot(std::hypot(double(q.w), double(q.x)), std::hypot(double(q.y), double(q.z)));
  require(n > 0, "zero regional attitude");
  q = {float(q.w/n), float(q.x/n), float(q.y/n), float(q.z/n)};
  q = rotation_ * q; q.normalize(); return q;
}
float Change::heading(float degrees) const {
  require(finite(degrees), "non-finite regional heading");
  const float result = float(wrapped(double(degrees) + headingDelta_));
  return result >= 360.f ? 0.f : result;
}
} // namespace regionTravel
