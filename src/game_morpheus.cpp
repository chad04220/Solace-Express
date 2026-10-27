// Somnium Volandi - XR-50 Morpheus systems (docs/XR50.md): its rocket, lit and shut down by the pilot, and what the
// craft tells its pilot as it goes - its engines changing mode, its leading edges near their limit
#include "game.h"

void Game::morpheusControls(float dt) {
  // X, or a double tap of gamepad A (A stays the wheel brake: the XR-40's cloak control): the rocket. Lit, it burns at
  // the throttle - 40% at the least - until it is shut down or its oxidizer is gone (aircraft_morpheus.cpp)
  SomnusState& W = somnus;   // (the double tap's clock is the XR-40's: one craft flies at a time)
  bool dblA = false;
  W.padATap += dt;
  if (actPadP(ACT_BRAKE) && !showMap) {
    if (W.padATap < 0.35f) { dblA = true; W.padATap = 9.f; } else W.padATap = 0;
  }
  const vec3 AMBER(1.f, 0.72f, 0.3f), RED(1.f, 0.45f, 0.35f);
  if (actPressed(ACT_CLOAK) || dblA) {
    if (plane.rocketLit) { plane.rocketLit = false; toast("ROCKET SHUT DOWN", AMBER); }
    else if (plane.oxidizer <= 0.f) toast("ROCKET: its oxidizer is spent", RED);
    else if (plane.fuel <= 0.f) toast("ROCKET: no fuel", RED);
    else {
      plane.rocketLit = true;
      toast(fmt("ROCKET LIT - %.0f s of oxidizer at full thrust", morpheusRocketSeconds(plane.oxidizer, 1.f)), AMBER);
      g_audio.triggerLocal(SFX_REHEAT, kMorpheusRocket, 0.9f);
    }
  }
  if (plane.rocketLit || plane.rocketN > 1.f) morpheusRocketOut = plane.oxidizer <= 0.f;
  else if (morpheusRocketOut) { morpheusRocketOut = false; toast("ROCKET OUT - oxidizer spent", AMBER); }
  // the engines' mode as it changes (the turbofans handing over to the ramjets, the ramjets to the scramjets), once it has
  // held for a second
  int mode = 0;
  if (plane.tbcc[1] > plane.tbcc[0] && plane.tbcc[1] >= plane.tbcc[2]) mode = 1;
  else if (plane.tbcc[2] > plane.tbcc[0] && plane.tbcc[2] > plane.tbcc[1]) mode = 2;
  if (mode != morpheusMode) {
    morpheusModeT += dt;
    if (morpheusModeT > 1.f) {
      if (plane.engineRunning && !plane.onGround)
        toast(mode == 2 ? "SCRAMJETS - supersonic combustion" : mode == 1 ? "RAMJETS - the turbofans shut down" : "TURBOFANS", vec3(0.55f, 0.85f, 1.f));
      morpheusMode = mode; morpheusModeT = 0.f;
    }
  } else morpheusModeT = 0.f;
  // the leading edges: a warning past 1,550 C, again if they keep heating; the limit is 1,650 (burned through)
  if (plane.skinT > kMorpheusSkinWarn && !morpheusHot) {
    morpheusHot = true;
    toast(fmt("LEADING EDGES %.0f C - slow down or climb", plane.skinT - 273.15f), RED);
    g_audio.trigger(SFX_BEEP, 0.8f);
  } else if (plane.skinT < kMorpheusSkinWarn - 60.f) morpheusHot = false;
}
