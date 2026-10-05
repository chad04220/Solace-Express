// Solace Express - the XR-15 Peregrine's belly Gatling (Codex's FX-27 rotary emitter): Y deploys it (doors, then the
// carrier lowers), the trigger spins the rotor up and it fires tracer rounds on the bolt path the XR-40's lasers use
// (the same sweep, impacts and damage), twelve a second with a little dispersion and a muzzle flash. It needs to be
// airborne with the gear up; it is safed and stowed in the reverse order. Y again safes it.
#include "game.h"

#include <cstdlib>
static float frand() { return (rand() % 10000) * 0.0001f; }

void Game::kestrelControls(float dt) {
  (void)dt;
  kestrelInput = KestrelGatlingInput();
  kestrelInput.enabled = !paused && !crashed && !plane.ev.crashed && !showMap && !showRadio && inputContext() == CTX_FLIGHT;
  kestrelInput.airborne = !plane.onGround;
  kestrelInput.gearClear = plane.gear <= .02f && !plane.ctl.gearDown;
  kestrelInput.toggleDeploy = kestrelInput.enabled && (actKeyP(ACT_WEAPONS) || (kestrelInput.airborne && actPadP(ACT_WEAPONS)));
  // every trigger is observed for the release latch, even when the pad is not yet armed
  kestrelInput.fireHeld = in.mDown[0] || actKey(ACT_FIRE) || actPad(ACT_FIRE);
  if (kestrelInput.toggleDeploy) {
    toast(kestrel.deployed ? "GATLING SAFE - stopping rotor, then stowing" : "GATLING DEPLOYING - doors, then carrier", vec3(1.f, 0.62f, 0.25f));
    g_audio.trigger(SFX_GEAR_CLUNK, 0.6f);
  }
  if (kestrelInput.enabled && kestrel.deployed && kestrelInput.airborne && !kestrelInput.gearClear &&
      (in.mPressed[0] || actKeyP(ACT_FIRE) || actPadP(ACT_FIRE)))
    toast("GATLING GEAR LOCK - retract gear, then release trigger", vec3(1.f, 0.7f, 0.3f));
}

void Game::updateKestrel(float dt) {
  if (!plane.spec || plane.spec - kAircraft != kPeregrine) return;
  kestrelInput.airborne = !plane.onGround;
  kestrelInput.gearClear = plane.gear <= .02f && !plane.ctl.gearDown;
  kestrelInput.enabled = kestrelInput.enabled && !paused && !showMap && !showRadio && !crashed && !plane.ev.crashed;
  if (crashed || plane.ev.crashed) { kestrel.deployed = false; kestrelInput.toggleDeploy = false; }
  const int shots = kestrel.step(dt, kestrelInput);
  kestrelInput.toggleDeploy = false;
  // updateWraith has already advanced the old bolts this frame: the new rounds leave the muzzle where it is after the
  // physics step, then join the same swept collision path
  if (shots > 0) {
    // the barrel at the top of the rotor follows the same angle as the shader's six-tube cluster
    const vec3 muzzle(.105f * float(std::cos(kestrel.rotorAngle)),
                      -.52f - .75f * float(kestrel.carrier) + .105f * float(std::sin(kestrel.rotorAngle)), -2.40f);
    const vec3 at = plane.pos + plane.q.rotate(muzzle);
    const vec3 fwd = plane.forward(), right = plane.right(), up = plane.up();
    for (int i = 0; i < shots && wraith.bolts.size() < 64; ++i) {
      vec3 d = normalize(fwd + (right * (frand() - 0.5f) + up * (frand() - 0.5f)) * 0.012f);   // ~0.35 deg of dispersion
      wraith.bolts.push_back({at, plane.vel + d * 2000.f, d, 0.f, 0.f, 1.2f, false, true});
      wraith.shots++;
    }
    g_audio.trigger(SFX_GATLING, 0.7f);
    spawn(at, plane.vel, 0.045f, 0.55f + 0.25f * frand(), 1.5f, vec3(1.f, 0.78f, 0.35f) * 3.f, 1.f, SPR_GLOW, 0.f, 0.f);   // muzzle flash
  }
}

void Game::kestrelVisual(FrameParams& fp) {
  if (plane.spec && plane.spec - kAircraft == kPeregrine && fp.plane.on) kestrel.pack(fp.plane.wr);
}
