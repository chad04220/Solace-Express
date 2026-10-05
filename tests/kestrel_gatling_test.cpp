// Production CPU state: order, reversals, input latches and frame-rate independence.
#include "../src/kestrel_gatling.h"
#include <cstdio>
#include <limits>
#include <random>
static int checks = 0, failures = 0;
static void check(bool good, const char* label) { ++checks; if (!good) { ++failures; std::printf("FAIL: %s\n", label); } }
static bool near(double a, double b, double eps = 1e-9) { return std::fabs(a-b) < eps; }
static void invariant(const KestrelGatlingState& s) {
  check(s.door >= 0 && s.door <= 1 && s.carrier >= 0 && s.carrier <= 1 && s.spin >= 0 && s.spin <= 1, "bounded motion channels");
  check(s.carrier == 0 || s.door == 1, "carrier only passes fully open doors");
  check(s.spin == 0 || (s.door == 1 && s.carrier == 1), "spin only with fully lowered carrier");
  check(!s.firing || (s.deployed && s.door == 1 && s.carrier == 1 && s.spin == 1), "firing requires complete ready state");
  check(std::isfinite(s.rotorAngle) && s.rotorAngle >= 0 && s.rotorAngle < s.tau, "bounded accumulated angle");
  check(s.flash >= 0 && s.flash <= 1, "bounded flash");
}
int main() {
  KestrelGatlingState s;
  KestrelGatlingInput in; in.airborne = true;
  check(s.fireLatch && !s.deployed && s.door == 0, "fresh flight starts contained and latched");
  in.fireHeld = true; in.toggleDeploy = true;
  check(s.step(3, in) == 0 && s.spin == 0, "held launch input does not fire or spin");
  in.toggleDeploy = false; in.fireHeld = false; s.step(.01, in);
  check(!s.fireLatch, "release clears launch latch");
  in.fireHeld = true;
  check(s.step(.54, in) == 0 && !s.firing, "spin-up waits for full speed");
  check(s.step(.01, in) == 1 && s.firing, "first bolt leaves at full spin");
  check(s.step(1, in) == 12, "steady arcade cadence is twelve bolts per second");
  const double angle = s.rotorAngle;
  in.toggleDeploy = true;
  check(s.step(.2, in) == 0 && !s.firing && s.carrier == 1 && s.door == 1 && s.spin > 0, "safe stops fire before rotor slows, carrier stays down");
  check(!near(angle, s.rotorAngle), "rotor integrates smoothly while slowing");
  in.toggleDeploy = false; s.step(.25, in);
  check(s.spin == 0 && s.carrier == 1 && s.door == 1, "rotor stops before any retraction");
  s.step(.4, in); check(near(s.carrier, .5) && s.door == 1, "doors wait through carrier retraction");
  s.step(.4, in); check(s.carrier == 0 && s.door == 1, "carrier fully stows before doors close");
  s.step(.7, in); check(s.carrier == 0 && s.door == 0 && s.spin == 0, "full ordered safe cycle");

  // Partitioned and single-step integration cross the very same stage boundaries.
  auto run = [](double h) {
    KestrelGatlingState r; r.fireLatch = false; KestrelGatlingInput i; i.airborne = true; i.fireHeld = true; i.toggleDeploy = true;
    double t = 0; int shots = 0;
    while (t < 4 - 1e-10) { double step = std::min(h, 4-t); shots += r.step(step, i); i.toggleDeploy = false; t += step; }
    return std::make_pair(r, shots);
  };
  auto reference = run(4);
  for (double h : {1.0/30, 1.0/60, 1.0/120, 1.0/144, .047}) {
    auto other = run(h);
    check(other.second == reference.second, "cadence independent of frame partitions");
    check(near(other.first.rotorAngle, reference.first.rotorAngle, 1e-8), "rotation independent of frame partitions");
    check(near(other.first.flash, reference.first.flash, 1e-8), "flash envelope independent of frame partitions");
  }
  s = KestrelGatlingState(); in = KestrelGatlingInput(); in.toggleDeploy = true;
  s.step(2, in); in.toggleDeploy = false; in.fireHeld = true;
  check(s.step(3, in) == 0 && s.spin == 0 && s.flash == 0, "ground firing and spinning disabled");
  in.airborne = true; check(s.step(1, in) == 0, "ground-held trigger stays latched after takeoff");
  in.fireHeld = false; s.step(.01, in); in.fireHeld = true;
  check(s.step(.6, in) > 0, "fresh airborne trigger works");
  in.enabled = false; check(s.step(.1, in) == 0 && !s.firing && s.flash == 0 && s.fireLatch, "overlay disables emission and relatches");
  in.enabled = true; check(s.step(1, in) == 0, "closing overlay cannot resume held fire");
  in.fireHeld = false; s.step(.01, in); in.fireHeld = true; check(s.step(.6, in) > 0, "release after overlay restores trigger");
  in.gearClear = false;
  check(s.step(.01, in) == 0 && !s.firing && s.flash == 0 && s.fireLatch, "gear block immediately stops fire/flash and latches held trigger");
  s.step(.5, in); check(s.spin == 0 && s.carrier == 1, "gear block stops powered spin without stowing inspection pose");
  in.gearClear = true; check(s.step(1, in) == 0, "clearing gear cannot restart held fire");
  in.fireHeld = false; s.step(.01, in); in.fireHeld = true;
  check(s.step(.6, in) > 0, "fresh trigger after gear clearance fires");
  const double oldAngle = s.rotorAngle;
  for (double dt : {0.0, -1.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
    check(s.step(dt, in) == 0 && s.rotorAngle == oldAngle, "invalid timestep is a no-op");

  // Repeated partial deployment reversals and interrupted trigger states.
  std::mt19937 random(2708);
  s = KestrelGatlingState(); in = KestrelGatlingInput();
  for (int frame = 0; frame < 40000; ++frame) {
    in.toggleDeploy = random()%67 == 0; in.fireHeld = random()%10 != 0;
    in.enabled = random()%83 != 0; in.airborne = random()%41 != 0; in.gearClear = random()%31 != 0;
    const double previousCarrier = s.carrier;
    const int shots = s.step(double(1 + random()%80)/1000, in);
    invariant(s);
    check(shots == 0 || (in.enabled && in.airborne && in.gearClear && in.fireHeld), "all emitted bolts require active airborne gear-clear trigger");
    check(s.carrier >= previousCarrier || s.spin == 0, "carrier never retracts with moving rotor");
  }
  float packed[7][4]; for (auto& v : packed) for (float& x : v) x = 99;
  s.pack(packed);
  check(near(packed[0][0], s.door, 1e-6) && near(packed[0][1], s.carrier, 1e-6) && near(packed[0][2], s.rotorAngle, 1e-6) && near(packed[0][3], s.spin, 1e-6), "render channels pack exact CPU pose");
  for (int i = 2; i < 7; ++i) for (float x : packed[i]) check(x == 0, "unused render channels cleared");
  std::printf("FX-27 Gatling state: %d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
