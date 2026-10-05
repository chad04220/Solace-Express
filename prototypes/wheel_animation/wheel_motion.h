// Shared cosmetic rolling state. Does not alter contacts, friction or flight forces.
#ifndef SOLACE_PREPARED_WHEEL_MOTION_H
#define SOLACE_PREPARED_WHEEL_MOTION_H
#include <cmath>
#include <algorithm>

struct WheelMotion {
  // Keep signed travel in double precision; resolve radians at the real visual tyre radius.
  // This keeps AircraftSpec/ModelDef and save files unchanged and avoids long-flight float drift.
  double travel = 0.0;
  float speed = 0.f;
  void reset() { travel = 0.0; speed = 0.f; }
  void step(float dt, float rollingSpeed, bool contact, float brake = 0.f) {
    if (!(dt > 0.f) || !std::isfinite(dt) || !std::isfinite(rollingSpeed)) return;
    if (contact) {
      // Braking changes ground velocity in the existing solver. Do not artificially freeze a rolling tyre.
      speed = std::fabs(rollingSpeed) < 0.01f ? 0.f : rollingSpeed;
      travel += double(speed) * dt;
    } else {
      // An unloaded wheel coasts, rather than following airspeed. Main-wheel brakes arrest it faster.
      float b = std::isfinite(brake) ? std::clamp(brake, 0.f, 1.f) : 0.f;
      const double rate = 0.125 + 20.0 * b;
      const double decay = std::exp(-rate * dt);
      travel += double(speed) * (-std::expm1(-rate * dt)) / rate;
      speed = float(speed * decay);
      if (std::fabs(speed) < 0.001f) speed = 0.f;
    }
  }
  // Positive rotation around local +X. Aircraft face -Z; airport scenery faces +Z.
  float angle(float radius, float forwardSign = -1.f) const {
    if (!(radius > 0.f) || !std::isfinite(radius) || !std::isfinite(travel)) return 0.f;
    return float(std::remainder(forwardSign * travel / radius, 6.2831853071795864769));
  }
};

#endif
