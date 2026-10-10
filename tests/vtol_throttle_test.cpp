// Real flight/input integration for the XR-40's momentary hover throttle. No window or GL context needed.
#include "../src/game.h"
#include <cmath>

struct GameTest {
  static void prepare(Game& g, int model = kWraith, float notch = 1.f) {
    g.initHeadless();
    g.screen = SCR_FLIGHT; g.specIdx = model; g.set.traffic = false;
    g.freeFlight = g.isolatedFlight = true; g.engineAutoStarted = g.takeoffAnnounced = true;
    g.contract.from = 0; g.contract.to = 1; g.contract.startAirborne = true;
    g.plane.reset(&kAircraft[model], g_world.airports[0].pos() + vec3(0, 1500, 0), 0,
                  kAircraft[model].maxFuel * 0.7f, 85.f, true, 0.f);
    g.plane.sceneryHits = false; g.plane.ctl.throttle = 0.45f;
    g.flapNotch = g.plane.ctl.flaps = g.plane.flaps = g.plane.nozzle = notch;
    g.armInputs(); // enter the flight context with no controls held
  }

  static int run() {
    g_world.build();
    int checks = 0, fails = 0;
    auto check = [&](bool value, const char* label) {
      ++checks;
      if (!value) { ++fails; printf("FAIL: %s\n", label); }
    };
    auto near = [](float a, float b) { return fabsf(a - b) < 1e-5f; };
    constexpr float dt = 1.f / 60.f;
    {
      Game g; prepare(g);
      g.flightControls(dt);
      check(g.plane.ctl.throttle == 0.f, "unheld VTOL entry clears inherited throttle");
      for (int key : {K_SHIFT, K_PGUP, K_PLUS}) {
        g.in.down[key] = true; g.flightControls(dt);
        check(g.plane.ctl.throttle == 1.f, "held digital throttle commands full hover power");
        g.in.endFrame(); g.flightControls(dt);
        check(g.plane.ctl.throttle == 1.f, "held digital power does not need repeated press edges");
        g.in.down[key] = false; g.flightControls(dt);
        check(g.plane.ctl.throttle == 0.f, "digital throttle release commands idle immediately");
      }
      for (int digit = 1; digit <= 9; ++digit) {
        g.in.down['0' + digit] = true; g.in.pressed['0' + digit] = true;
        g.flightControls(dt);
        check(near(g.plane.ctl.throttle, digit / 9.f), "held number is a proportional VTOL power target");
        g.in.endFrame(); g.flightControls(dt);
        check(near(g.plane.ctl.throttle, digit / 9.f), "number target stays live while held");
        g.in.down['0' + digit] = false; g.flightControls(dt);
        check(g.plane.ctl.throttle == 0.f, "number release cannot latch hover power");
      }
      g.in.pressed['9'] = true; g.flightControls(dt);
      check(g.plane.ctl.throttle == 0.f, "stale number press edge without a held key cannot power hover");
      g.in = Input(); g.in.down[K_SHIFT] = true; g.in.down['3'] = true;
      g.flightControls(dt);
      check(g.plane.ctl.throttle == 1.f, "digital full-power hold wins over a partial preset");
      for (int cut : {int(K_CTRL), int(K_PGDN), int(K_MINUS), int('0')}) {
        g.in.down[cut] = true; g.flightControls(dt);
        check(g.plane.ctl.throttle == 0.f, "held idle control wins over simultaneous power holds");
        g.in.down[cut] = false;
      }
      g.in = Input(); g.set.keyBind[ACT_THR_UP] = 'J'; g.set.keyBind[ACT_THR_DN] = 'K';
      g.in.down['J'] = true; g.flightControls(dt);
      check(g.plane.ctl.throttle == 1.f, "rebound keyboard throttle is momentary");
      g.in.down['K'] = true; g.flightControls(dt);
      check(g.plane.ctl.throttle == 0.f, "rebound throttle-down commands idle");
      g.in = Input(); g.flightControls(dt);
      check(g.plane.ctl.throttle == 0.f, "rebound keyboard release leaves no power");
    }
    {
      Game g; prepare(g); g.in.pad = true;
      for (float rt : {0.15f, 0.35f, 0.7f, 1.f}) {
        g.in.rt = rt; g.flightControls(dt);
        check(near(g.plane.ctl.throttle, rt), "right trigger preserves proportional hover power");
        g.in.rt = 0; g.flightControls(dt);
        check(g.plane.ctl.throttle == 0.f, "trigger release commands idle");
      }
      g.in.rt = 0.035f; g.flightControls(dt);
      check(g.plane.ctl.throttle == 0.f, "neutral trigger noise cannot sustain hover power");
      g.in.rt = 0.75f; g.in.lt = 0.25f; g.flightControls(dt);
      check(near(g.plane.ctl.throttle, 0.5f), "left trigger proportionally reduces right-trigger power");
      g.in.lt = 1.f; g.flightControls(dt);
      check(g.plane.ctl.throttle == 0.f, "opposing triggers clamp to idle rather than negative power");
      g.in.rt = 0.4f; g.in.lt = 0.f; g.in.down[K_SHIFT] = true; g.flightControls(dt);
      check(g.plane.ctl.throttle == 1.f, "keyboard hold and partial trigger choose full power without adding beyond full");
      g.in.down[K_SHIFT] = false; g.flightControls(dt);
      check(near(g.plane.ctl.throttle, 0.4f), "releasing keyboard returns to the still-held proportional trigger");
      g.in.down['6'] = true; g.flightControls(dt);
      check(near(g.plane.ctl.throttle, 6.f / 9.f), "held number and trigger select the higher power target");
      g.in.down['6'] = false; g.in.down[K_CTRL] = true; g.flightControls(dt);
      check(g.plane.ctl.throttle == 0.f, "keyboard idle overrides a still-held controller trigger");
      g.in.down[K_CTRL] = false;
      g.in.lt = g.in.rt = 0.f; g.set.padBind[ACT_THR_UP] = PAD_UP; g.set.padBind[ACT_THR_DN] = PAD_DOWN;
      g.in.buttons = PAD_UP; g.flightControls(dt);
      check(g.plane.ctl.throttle == 1.f, "rebound controller button applies held power");
      g.in.buttons |= PAD_DOWN; g.flightControls(dt);
      check(g.plane.ctl.throttle == 0.f, "controller idle button wins a simultaneous power button");
      g.in.buttons = 0; g.flightControls(dt);
      check(g.plane.ctl.throttle == 0.f, "controller button release leaves idle");
      g.in.pad = false; g.in.rt = 1.f; g.flightControls(dt);
      check(g.plane.ctl.throttle == 0.f, "disconnected controller's stale trigger value is ignored");
    }
    {
      // Conventional planes, the XR-30, and partial pod tilts still retain their incremental throttle.
      for (int model = 0; model < kAircraftCount; ++model) {
        Game g; prepare(g, model, model == kWraith ? 0.f : 1.f);
        g.flightControls(dt);
        check(near(g.plane.ctl.throttle, 0.45f), "every non-hover aircraft retains its throttle on release");
        g.in.down[K_SHIFT] = true; g.flightControls(dt);
        float value = g.plane.ctl.throttle;
        check(near(value, 0.45f + 0.55f * dt), "normal flight retains incremental keyboard throttle");
        g.in.down[K_SHIFT] = false; g.flightControls(dt);
        check(near(g.plane.ctl.throttle, value), "normal flight still latches the selected power");
      }
      for (float notch : {0.f, 1.f / 3.f, 2.f / 3.f}) {
        Game g; prepare(g, kWraith, notch); g.in.pad = true; g.in.rt = 0.5f;
        g.flightControls(dt);
        const float value = 0.45f + 0.5f * 0.6f * dt;
        check(near(g.plane.ctl.throttle, value), "intermediate notches retain incremental analog throttle");
        g.in.rt = 0.f; g.flightControls(dt);
        check(near(g.plane.ctl.throttle, value), "intermediate notch release retains power");
        g.in.pressed['6'] = true; g.flightControls(dt); g.in.endFrame(); g.flightControls(dt);
        check(near(g.plane.ctl.throttle, 6.f / 9.f), "intermediate notches retain latched number presets");
      }
    }
    {
      Game g; prepare(g, kWraith, 2.f / 3.f); g.plane.ctl.throttle = 0.9f;
      g.set.keyBind[ACT_THR_UP] = 'J';
      g.in.pressed[g.set.keyBind[ACT_FLAPS_DN]] = true; g.flightControls(dt);
      check(g.plane.ctl.flaps > 0.99f && g.plane.nozzle < 0.99f && g.plane.ctl.throttle == 0.f,
            "selecting hover applies release-to-idle before the pods finish moving");
      bool hint = false;
      for (const auto& t : g.toasts) hint |= t.text.find("hold J; release to idle") != std::string::npos;
      check(hint, "hover entry explains the rebound hold control and release behavior");
      g.in.endFrame(); g.in.down['6'] = true; g.flightControls(dt);
      g.in.down['6'] = false; g.in.pressed[g.set.keyBind[ACT_FLAPS_UP]] = true; g.flightControls(dt);
      check(g.plane.ctl.flaps < 0.99f && near(g.plane.ctl.throttle, 6.f / 9.f),
            "leaving hover returns to the existing latched transition-notch controls");
      g.in.endFrame(); g.in.pressed[g.set.keyBind[ACT_FLAPS_DN]] = true; g.in.down['J'] = true;
      g.flightControls(dt);
      check(g.plane.ctl.flaps > 0.99f && g.plane.ctl.throttle == 1.f, "held power works in the same frame as hover selection");
      g.in = Input(); g.flightControls(dt);
      check(g.plane.ctl.throttle == 0.f, "release after reentering hover cannot inherit transition throttle");
    }
    {
      Game g; prepare(g); g.plane.apOn = true; g.plane.apMode = Plane::AP_APPR;
      g.plane.ctl.throttle = 0.62f; g.in.pad = true; g.in.rt = 1.f;
      g.flightControls(dt);
      check(g.plane.apOn && near(g.plane.ctl.throttle, 0.62f), "autoland owns the throttle even with a held trigger");
      g.in = Input(); g.flightControls(dt);
      check(g.plane.apOn && near(g.plane.ctl.throttle, 0.62f), "autoland throttle is not cut by released controls");
      g.in.down[K_DOWN] = true; g.flightControls(dt);
      check(!g.plane.apOn && g.plane.ctl.throttle == 0.f, "stick takeover clears autoland power in the same frame");
      g.in = Input(); g.plane.apOn = true; g.plane.apMode = Plane::AP_HOLD; g.plane.apSpeed = 150.f;
      g.plane.ctl.throttle = 0.62f; g.in.pad = true; g.in.rt = 0.5f;
      g.flightControls(dt);
      check(g.plane.apOn && near(g.plane.ctl.throttle, 0.62f) && g.plane.apSpeed > 150.f,
            "AP hold retains speed-target trigger behavior");
      g.in = Input(); g.in.pressed['5'] = true; g.flightControls(dt);
      check(g.plane.apOn && g.plane.apSpeed == 0.f && near(g.plane.ctl.throttle, 5.f / 9.f),
            "AP hold manual-throttle opt-out retains its existing number preset");
      g.in.endFrame(); g.flightControls(dt);
      check(near(g.plane.ctl.throttle, 5.f / 9.f), "engaged AP hold manual throttle remains unchanged on release");
      g.in.pressed[g.set.keyBind[ACT_AP]] = true; g.flightControls(dt);
      check(!g.plane.apOn && g.plane.ctl.throttle == 0.f, "AP switch takeover clears the inherited command immediately");
      g.in = Input(); g.plane.apOn = true; g.plane.apMode = Plane::AP_STUNT; g.plane.ctl.throttle = 0.7f;
      g.flightControls(dt);
      check(g.plane.apOn && near(g.plane.ctl.throttle, 0.7f), "untouched aerobatics keep autopilot throttle ownership");
    }
    {
      Game g; prepare(g); g.plane.engineSpool = 0.f;
      g.in.down[K_SHIFT] = true; g.flightControls(dt); g.plane.step(0.1f, g.wx, 0.f);
      check(g.plane.ctl.throttle == 1.f && g.plane.engineSpool > 0.f && g.plane.engineSpool < 0.3f,
            "held throttle still spools the engines up through the original physics");
      const float previous = g.plane.engineSpool;
      g.in.down[K_SHIFT] = false; g.flightControls(dt);
      check(g.plane.ctl.throttle == 0.f && g.plane.engineSpool == previous, "release changes the target without teleporting engine spool");
      g.plane.step(0.02f, g.wx, 0.1f);
      check(g.plane.engineSpool > 0.f && g.plane.engineSpool < previous, "released engines wind down gradually");
      g.plane.step(0.3f, g.wx, 0.12f);
      check(near(g.plane.engineSpool, previous * expf(-g.plane.spoolRate() * 0.32f)),
            "wind-down preserves the original exponential spool rate");
      g.plane.step(3.f, g.wx, 0.42f);
      check(g.plane.engineSpool < 0.001f && g.plane.engineRunning, "released engines approach idle without stopping the engines");
      g.in.down['6'] = true; g.in.pressed['6'] = true; g.update(0.2f);
      check(near(g.plane.ctl.throttle, 6.f / 9.f), "long-frame catch-up retains held preset after press edges are consumed");
      g.in = Input(); g.update(0.2f);
      check(g.plane.ctl.throttle == 0.f, "long-frame catch-up sees release in every physics step");
    }
    {
      Game g; prepare(g); g.in.pad = true; g.in.rt = 0.8f; g.update(dt);
      check(near(g.plane.ctl.throttle, 0.8f), "live game loop applies proportional trigger power");
      const float spool = g.plane.engineSpool;
      g.paused = true; g.update(dt);
      check(g.plane.engineSpool == spool, "pause still freezes engine physics");
      g.paused = false; g.update(dt);
      check(g.plane.ctl.throttle == 0.f, "held trigger cannot reapply hover power on resume");
      g.update(dt);
      check(g.plane.ctl.throttle == 0.f, "trigger stays unarmed until physically released");
      g.in.rt = 0.f; g.update(dt); g.in.rt = 0.6f; g.update(dt);
      check(near(g.plane.ctl.throttle, 0.6f), "release and fresh trigger hold rearm hover power");
      g.in.pad = false; g.update(dt);
      check(g.paused, "controller disconnect retains automatic pause safety");
      g.paused = false; g.update(dt);
      check(g.plane.ctl.throttle == 0.f, "keyboard continuation after disconnect clears stale hover power");
      g.paused = true; g.in.pad = true; g.in.rt = 1.f; g.update(dt);
      g.paused = false; g.update(dt);
      check(g.plane.ctl.throttle == 0.f, "reconnecting with a trigger held does not restore power on resume");
      g.in.rt = 0.f; g.update(dt); g.in.rt = 0.4f; g.update(dt);
      check(near(g.plane.ctl.throttle, 0.4f), "reconnected trigger works after a neutral release");
      g.in = Input(); g.update(dt); g.paused = false; g.update(dt);
      g.in.down[K_SHIFT] = true; g.update(dt);
      check(g.plane.ctl.throttle == 1.f, "keyboard can take over after disconnect");
      g.focusLost(); g.update(dt); g.paused = false; g.update(dt);
      check(g.plane.ctl.throttle == 0.f, "focus loss and resume do not retain held keyboard power");
    }
    {
      Game g; prepare(g); g.in.pad = true; g.in.rt = 0.6f; g.flightControls(dt);
      g.showMap = true; g.armInputs(); g.flightControls(dt);
      check(g.plane.ctl.throttle == 0.f, "opening GPS rearms a held hover trigger just like held buttons");
      g.in.rt = 0.f; g.armInputs(); g.in.rt = 0.6f; g.flightControls(dt);
      check(near(g.plane.ctl.throttle, 0.6f), "fresh proportional power is available while the GPS overlay is open");
      g.showMap = false; g.armInputs(); g.flightControls(dt);
      check(g.plane.ctl.throttle == 0.f, "closing GPS also requires releasing the held hover trigger");
      g.in.rt = 0.f; g.armInputs(); g.in.pad = false; g.in.down['6'] = true;
      g.in.down[K_SHIFT] = true; g.paused = true; g.armInputs();
      g.paused = false; g.armInputs(); g.flightControls(dt);
      check(g.plane.ctl.throttle == 0.f, "pause context quarantines digital and preset power holds");
    }
    printf("VTOL hold throttle: %d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
  }
};

int main() { return GameTest::run(); }
