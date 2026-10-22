// Surface-chart travel between procedural Earth regions. Physics remains local:
// +x east, +z south, y altitude. This is not native ECEF/orbital simulation.
#pragma once
#include "geo.h"

namespace regionTravel {
class Chart {
public:
  explicit Chart(geo::Position origin = geo::solaceOrigin());
  geo::Position origin() const { return origin_; }
  geo::Position position(vec3 local) const;
  geo::Double3 localDouble(geo::Position position) const;
  vec3 local(geo::Position position) const;
  // Heading in this chart toward a great-circle target, measured at local focus.
  // nullopt at coincident or antipodal targets; degrees clockwise from -z.
  std::optional<double> bearing(vec3 focus, geo::Position target) const;
private:
  geo::Position origin_;
  geo::DestinationFrom from_;   // (position()'s every point from the origin: its frame made once)
};

class Change {
public:
  Change(const Chart& from, const Chart& to, vec3 focus);
  vec3 point(vec3 local) const;     // exact geographic re-expression; same altitude
  vec3 vector(vec3 local) const;    // north-convergence rotation; preserves speed/up
  quat attitude(quat local) const;
  float heading(float degrees) const;
  double headingDelta() const { return headingDelta_; }
private:
  Chart from_, to_;
  double headingDelta_ = 0;
  double horizontalCos_ = 1, horizontalSin_ = 0;
  quat rotation_;
};
} // namespace regionTravel
