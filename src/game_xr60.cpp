// Somnium Volandi - XR-60 Phantasos systems (docs/XR60.md): its surge, lit and shut off by the pilot, and what the
// craft tells its pilot as it goes - its drive changing mode, its skin near its limit, its bank spent and recharged
#include "game.h"

void Game::xr60Controls(float dt) {
  // A double press of A - the keyboard's (two short taps: a held A is the roll it always was) or the gamepad's (A stays
  // the wheel brake) - or X: the surge, shut off and lit again as often as the pilot likes while the bank holds. Lit,
  // it triples the ring until the bank is spent; off, the bank recharges (aircraft_xr60.cpp)
  SomnusState& W = somnus;   // (the pad's double tap's clock is the XR-40's: one craft flies at a time)
  bool dblA = false;
  W.padATap += dt;
  if (actPadP(ACT_BRAKE) && !showMap) {
    if (W.padATap < 0.35f) { dblA = true; W.padATap = 9.f; } else W.padATap = 0;
  }
  morpheusKeyTap += dt;   // (the XR-50's double-tap clock: one craft flies at a time)
  if (in.down['A']) morpheusKeyHeld += dt; else morpheusKeyHeld = 0.f;
  if (morpheusKeyHeld > 0.25f) morpheusKeyTap = 9.f;
  if (in.pressed['A'] && !showMap) {
    if (morpheusKeyTap < 0.35f) { dblA = true; morpheusKeyTap = 9.f; } else morpheusKeyTap = 0;
  }
  const vec3 VIO(0.75f, 0.45f, 1.f), CYN(0.45f, 0.9f, 1.f), RED(1.f, 0.45f, 0.35f);
  if (actPressed(ACT_CLOAK) || dblA) {
    if (plane.surgeOn) { plane.surgeOn = false; toast("SURGE OFF - the bank recharges", VIO); }
    else if (plane.surgeBank <= 0.02f) toast("SURGE: the bank is spent - wait for it", RED);
    else if (!plane.engineRunning || plane.fuel <= 0.f) toast("SURGE: the core is cold", RED);
    else {
      plane.surgeOn = true;
      toast(fmt("SURGE - x3 for %.0f s", plane.surgeBank * kXr60SurgeSeconds), VIO);
      g_audio.triggerLocal(SFX_REHEAT, kXr60Ring, 0.9f);
    }
  }
  if (plane.surgeOn) xr60Spent = plane.surgeBank <= 0.f;
  else if (xr60Spent) { xr60Spent = false; toast("SURGE OUT - bank spent, recharging", VIO); }
  // the drive's mode as it changes (the ring handing over to the scramjet in the dense air, and back as it thins), once
  // it has held for a second
  int mode = plane.scramN > plane.ringN ? 1 : 0;
  if (mode != xr60Mode) {
    xr60ModeT += dt;
    if (xr60ModeT > 1.f) {
      if (plane.engineRunning && !plane.onGround)
        toast(mode == 1 ? "SCRAMJET - the keel's duct carries it; the ring is quenched" : "RING DRIVE - the air thin enough for the ring again", CYN);
      xr60Mode = mode; xr60ModeT = 0.f;
    }
  } else xr60ModeT = 0.f;
  // the skin: a warning past 2,350 C, again if it keeps heating; the limit is 2,500 (the sheath can't stand it off)
  if (plane.skinT > kXr60SkinWarn && !xr60Hot) {
    xr60Hot = true;
    toast(fmt("SKIN %.0f C - slow down or climb", plane.skinT - 273.15f), RED);
    g_audio.trigger(SFX_BEEP, 0.8f);
  } else if (plane.skinT < kXr60SkinWarn - 80.f) xr60Hot = false;
}
