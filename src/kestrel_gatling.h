// FX-27's fictional rotary emitter: shared, deterministic animation and input state.
// Body geometry is driven by these channels, never by shader wall-clock time.
#pragma once
#include <algorithm>
#include <cmath>

struct KestrelGatlingInput {
  bool toggleDeploy = false;
  bool fireHeld = false;
  bool airborne = false;
  bool gearClear = true;  // gameplay requires actual gear <= .02 and an up command
  bool enabled = true;  // false in menus, overlays, after a crash or while paused
};

struct KestrelGatlingState {
  static constexpr double doorSeconds = 0.70;
  static constexpr double carrierSeconds = 0.80;
  static constexpr double spinUpSeconds = 0.55;
  static constexpr double spinDownSeconds = 0.45;
  static constexpr double rotorRadiansPerSecond = 25.0;
  static constexpr double shotPeriod = 1.0 / 12.0;
  static constexpr double flashSeconds = 0.10;
  static constexpr double tau = 6.2831853071795864769;
  bool deployed = false, fireLatch = true, firing = false;
  double door = 0, carrier = 0, rotorAngle = 0, spin = 0, flash = 0, shotClock = 0;

  void inhibit() { fireLatch = true; firing = false; flash = 0; shotClock = 0; }

  // Returns emitted arcade bolts this step. A release is required after launch/menu clicks,
  // disarming and a held ground/gear-blocked trigger. Y deploys explicitly; firing never auto-arms.
  // Exact linear stage integration makes the normal simulation independent of frame rate.
  // As elsewhere in the game, an exceptional hitch is bounded rather than replayed forever.
  int step(double dt, const KestrelGatlingInput& input) {
    if (!(dt > 0) || !std::isfinite(dt)) return 0;
    double left = std::min(dt, 60.0);
    if (!input.enabled) inhibit();
    if (input.enabled && input.toggleDeploy) {
      deployed = !deployed;
      if (!deployed) inhibit();
    }
    if ((!input.airborne || !input.gearClear) && input.fireHeld) fireLatch = true;
    if (input.enabled && !input.fireHeld) fireLatch = false;
    const bool trigger = input.enabled && input.airborne && input.gearClear && deployed && input.fireHeld && !fireLatch;
    if (!trigger) { firing = false; flash = 0; shotClock = 0; }

    // Integrate rotor angle analytically through a linear acceleration/deceleration ramp.
    auto advance = [&](double h, double target) {
      const double rate = 1.0 / (target > spin ? spinUpSeconds : spinDownSeconds);
      const double moving = std::min(h, std::fabs(target - spin) / rate);
      const double next = spin + (target > spin ? 1 : -1) * rate * moving;
      rotorAngle = std::fmod(rotorAngle + rotorRadiansPerSecond *
                            ((spin + next) * 0.5 * moving + target * (h - moving)), tau);
      spin = std::clamp(next, 0.0, 1.0);
      if (std::fabs(spin - target) < 1e-12) spin = target;
      flash = std::max(0.0, flash - h / flashSeconds);
    };
    auto move = [&](double& value, double target, double seconds) {
      const double need = std::fabs(target - value) * seconds;
      const double h = std::min(left, need);
      advance(h, 0.0);
      value += (target > value ? 1 : -1) * h / seconds;
      if (h >= need - 1e-12) value = target;
      value = std::clamp(value, 0.0, 1.0);
      left = std::max(0.0, left - h);
    };

    if (deployed) {
      // The carrier never moves through partially open doors.
      if (door < 1) move(door, 1, doorSeconds);
      if (door == 1 && carrier < 1) move(carrier, 1, carrierSeconds);
    } else {
      // Rotation stops completely before the carrier rises; doors wait for full stow.
      if (spin > 0) {
        const double h = std::min(left, spin * spinDownSeconds);
        advance(h, 0); left = std::max(0.0, left - h);
      }
      if (spin == 0 && carrier > 0) move(carrier, 0, carrierSeconds);
      if (spin == 0 && carrier == 0 && door > 0) move(door, 0, doorSeconds);
    }

    if (!(trigger && door == 1 && carrier == 1)) {
      advance(left, 0); firing = false; shotClock = 0;
      return 0;
    }
    if (spin < 1) {
      const double h = std::min(left, (1 - spin) * spinUpSeconds);
      advance(h, 1); left = std::max(0.0, left - h);
    }
    if (spin < 1) { firing = false; shotClock = 0; return 0; }
    firing = true;
    advance(left, 1);
    int shots = 0;
    if (left + 1e-12 >= shotClock) {
      const double count = 1 + std::floor(std::max(0.0, left - shotClock + 1e-12) / shotPeriod);
      const double lastShot = shotClock + (count - 1) * shotPeriod;
      flash = std::clamp(1 - (left - lastShot) / flashSeconds, 0.0, 1.0);
      shotClock += count * shotPeriod;
      shots = int(std::min(count, 64.0));  // bounded world-FX work after exceptional hitches
    }
    shotClock = std::max(0.0, shotClock - left);
    return shots;
  }

  // The seven existing renderer vectors are per-craft. Zero unused channels deliberately.
  void pack(float wr[7][4]) const {
    for (int i = 0; i < 7; ++i) for (int j = 0; j < 4; ++j) wr[i][j] = 0;
    wr[0][0] = float(door); wr[0][1] = float(carrier);
    wr[0][2] = float(rotorAngle); wr[0][3] = float(spin);
    wr[1][0] = float(flash); wr[1][1] = firing ? 1.f : 0.f; wr[1][2] = deployed ? 1.f : 0.f;
  }
};
