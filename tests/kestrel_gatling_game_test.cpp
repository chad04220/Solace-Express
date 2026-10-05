// The real Game input dispatch, post-physics emitter, stock bolt path and render packing.
#include "../src/game.h"
#include <cstdio>
struct GameTest {
  static int run() {
    int checks = 0, failures = 0;
    auto check = [&](bool good, const char* label) { ++checks; std::printf("%s %s\n", good ? "PASS" : "FAIL", label); failures += !good; };
    g_world.build(); buildStory(); g_audio.init(48000);
    static Game g; g.initHeadless();
    g.startFlight(g_story[0], kPeregrine, Career::SRC_NONE);
    g.screen = SCR_FLIGHT; g.ctx = g.lastCtx = Game::CTX_FLIGHT;
    g.plane.pos = vec3(0, 8000, 0); g.plane.q = quat(); g.plane.vel = vec3(0, 0, -100);
    g.plane.onGround = false; g.plane.ev.crashed = g.crashed = false;
    g.plane.gear = 0; g.plane.ctl.gearDown = false;
    auto step = [&](float dt) { g.kestrelControls(dt); g.updateKestrel(dt); g.in.endFrame(); };
    auto advance = [&](int n) { for (int k = 0; k < n; ++k) step(.01f); };
    check(!g.kestrel.deployed && g.kestrel.fireLatch, "flight reset contains gun and latches launch input");
    g.in.mDown[0] = true; g.in.pressed['Y'] = true; step(.01f); advance(220);
    check(g.kestrel.deployed && g.kestrel.carrier == 1 && g.wraith.bolts.empty(), "Y deploys, launch click cannot fire");
    g.in.mDown[0] = false; step(.01f); g.in.down[K_ENTER] = true; advance(56);
    check(!g.wraith.bolts.empty() && g.kestrel.firing, "Enter emits actual game bolts after spin-up");
    if (!g.wraith.bolts.empty()) {
      const auto b = g.wraith.bolts.front();
      const vec3 bodyMuzzle = g.plane.q.conj().rotate(b.h - g.plane.pos);
      check(std::fabs(bodyMuzzle.z + 2.40f) < 1e-4f && std::fabs(length(vec2(bodyMuzzle.x, bodyMuzzle.y + 1.27f)) - .105f) < 1e-3f, "bolt starts on a deployed rotating barrel muzzle");
      vec3 rel = b.v - g.plane.vel;
      check(std::fabs(length(rel) - 2000.f) < 1e-2f && dot(normalize(rel), g.plane.forward()) > cosf(1.f * DEG) && b.tracer, "round inherits aircraft velocity, leaves within the dispersion cone as a tracer");
      TrafficCraft target; target.spec = 0; target.role = TrafficCraft::CRUISER; target.pos = b.h + g.plane.forward()*35.f;
      g.traffic.craft.push_back(target); g.updateWraith(.03f);
      check(!g.traffic.craft.front().alive && g.wraith.kills == 1, "Kestrel bolts enter existing swept collision/impact path");
    }
    g.wraith.bolts.clear(); g.in.down[K_ENTER] = false; step(.01f);
    g.in.mDown[0] = true; g.showMap = true; advance(100);
    check(g.wraith.bolts.empty() && !g.kestrel.firing && g.kestrel.flash == 0, "map mouse input does not emit");
    g.showMap = false; advance(100);
    check(g.wraith.bolts.empty() && g.kestrel.fireLatch, "closing map requires mouse release");
    g.in.mDown[0] = false; step(.01f); g.in.mDown[0] = true; advance(60);
    check(!g.wraith.bolts.empty(), "fresh mouse trigger after map emits");
    g.wraith.bolts.clear(); g.showRadio = true; advance(60);
    check(g.wraith.bolts.empty() && g.kestrel.fireLatch, "radio overlay inhibits and relatches trigger");
    g.showRadio = false; g.in.mDown[0] = false; step(.01f);
    g.plane.onGround = true; g.in.mDown[0] = true; advance(80);
    check(g.wraith.bolts.empty() && g.kestrel.spin == 0, "ground trigger cannot spin or fire");
    g.plane.onGround = false; advance(80);
    check(g.wraith.bolts.empty(), "held ground trigger remains safe after takeoff");
    g.in.mDown[0] = false; step(.01f);

    // Input dispatch: shared controller Y must not also toggle the landing gear.
    g.in = Input(); g.kestrel = KestrelGatlingState();
    g.plane.ctl.gearDown = true; g.in.pad = true; g.in.buttons = g.in.buttonsPressed = PAD_Y;
    g.flightControls(.01f); g.updateKestrel(.01f); g.in.endFrame();
    check(g.kestrel.deployed && g.plane.ctl.gearDown, "airborne gamepad Y deploys without changing gear");
    g.plane.ctl.gearDown = false;
    g.in.buttons = 0; advance(160); g.in.buttons = PAD_RB;
    g.plane.ctl.yaw = 0; g.flightControls(.01f);
    check(std::fabs(g.plane.ctl.yaw) < 1e-6f, "armed fire bumper does not apply rudder");
    advance(60); check(!g.wraith.bolts.empty(), "armed gamepad bumper fires");
    g.wraith.bolts.clear(); g.in.buttons = 0; g.in.down['Q'] = true; g.plane.ctl.yaw = 0; g.flightControls(.01f);
    check(g.plane.ctl.yaw < 0, "keyboard rudder remains available while armed");
    g.in = Input(); g.in.pressed['Y'] = true; step(.01f); advance(210);
    check(g.kestrel.door == 0 && g.kestrel.carrier == 0 && g.kestrel.spin == 0, "second Y completes ordered safe/stow cycle");

    // A paused update must freeze pose and require release after a resume click.
    g.kestrel.deployed = true; advance(160);
    auto gearBlock = [&](float actual, bool commandedDown, const char* label) {
      g.plane.gear = actual; g.plane.ctl.gearDown = commandedDown;
      g.in.mDown[0] = false; step(.01f); g.wraith.bolts.clear(); g.in.mDown[0] = true; advance(70);
      check(g.wraith.bolts.empty() && !g.kestrel.firing && g.kestrel.flash == 0 && g.kestrel.spin == 0 && g.kestrel.carrier == 1, label);
    };
    gearBlock(1, true, "gear down blocks firing/spin while deployment remains available");
    gearBlock(.5f, false, "gear retracting blocks firing/spin");
    gearBlock(.021f, false, "gear just outside clear threshold still blocks");
    gearBlock(0, true, "gear-down command immediately blocks even before physical motion");
    g.plane.ctl.gearDown = false; g.plane.gear = 0; advance(80);
    check(g.wraith.bolts.empty() && g.kestrel.fireLatch, "fully raised gear cannot resume a blocked held trigger");
    g.in.mDown[0] = false; step(.01f); g.in.mDown[0] = true; advance(60);
    check(!g.wraith.bolts.empty() && g.kestrel.firing, "fully raised gear with up command and fresh trigger fires");
    g.wraith.bolts.clear(); g.plane.ctl.gearDown = true; step(.01f);
    check(g.wraith.bolts.empty() && !g.kestrel.firing && g.kestrel.flash == 0, "lowering command interrupts live fire immediately");
    g.plane.ctl.gearDown = false; g.in.mDown[0] = false; step(.01f); g.in.mDown[0] = true; advance(60);
    g.paused = true; const double pose = g.kestrel.rotorAngle; g.update(.016f);
    check(g.kestrel.rotorAngle == pose && g.kestrel.fireLatch && !g.kestrel.firing, "pause freezes pose and inhibits trigger");
    g.paused = false; g.ctx = g.lastCtx = Game::CTX_FLIGHT; g.wraith.bolts.clear(); advance(100);
    check(g.wraith.bolts.empty(), "resume while mouse held cannot fire");

    g.in = Input(); step(.01f); g.set.keyBind[ACT_FIRE] = 'J'; g.in.down['J'] = true; advance(60);
    check(!g.wraith.bolts.empty(), "rebound keyboard fire action works");
    g.in = Input(); g.wraith.bolts.clear(); step(.01f);
    g.set.padBind[ACT_FIRE] = PAD_LB; g.in.pad = true; g.in.buttons = PAD_LB; g.plane.ctl.yaw = 0;
    g.flightControls(.01f); check(std::fabs(g.plane.ctl.yaw) < 1e-6f, "rebound fire bumper is removed from its matching rudder action");
    advance(60); check(!g.wraith.bolts.empty(), "rebound gamepad fire action works");
    g.wraith.bolts.clear(); g.plane.ev.crashed = true; advance(10);
    check(!g.kestrel.deployed && !g.kestrel.firing && g.wraith.bolts.empty(), "same-step crash event disarms and inhibits emission");
    g.plane.ev.crashed = false; g.in = Input(); step(.01f);

    FrameParams frame; frame.plane.on = true; g.kestrelVisual(frame);
    check(frame.plane.wr[0][0] == float(g.kestrel.door) && frame.plane.wr[0][2] == float(g.kestrel.rotorAngle), "game packs production state into shader channels");
    g.plane.spec = &kAircraft[kMantis]; frame.plane.wr[0][0] = .37f; g.kestrelVisual(frame); g.updateKestrel(1);
    check(frame.plane.wr[0][0] == .37f, "Mantis render state is untouched");
    g.plane.spec = &kAircraft[kWraith]; g.kestrelVisual(frame);
    check(frame.plane.wr[0][0] == .37f, "Wraith render state is untouched");
    g.set.resetBindings(); g.plane.spec = &kAircraft[kWraith]; g.in = Input(); g.in.pressed['Y'] = true;
    const bool wraithBefore = g.wraith.armed; const bool kestrelBefore = g.kestrel.deployed;
    g.flightControls(.01f);
    check(g.wraith.armed != wraithBefore && g.kestrel.deployed == kestrelBefore, "Wraith keeps its original Y control dispatch");
    g.plane.spec = &kAircraft[kMantis]; g.in = Input(); g.in.pressed['Y'] = true; const bool mantisGearBefore = g.plane.ctl.gearDown;
    const bool wraithAfter = g.wraith.armed;
    g.flightControls(.01f);
    check(g.plane.ctl.gearDown == mantisGearBefore && g.wraith.armed == wraithAfter && g.kestrel.deployed == kestrelBefore,
          "Generic Mantis Y has no weapon dispatch or Kestrel side effects");
    g.startFlight(g_story[0], kPeregrine, Career::SRC_NONE);
    check(!g.kestrel.deployed && g.kestrel.door == 0 && g.kestrel.fireLatch && g.wraith.bolts.empty(), "new flight clears gun state and old bolts");
    // Current-head registry names and selections must survive the appended research craft.
    check(Game::kNumResCards == 17, "the register's test cards: thirteen plus the XR-15's four");
    g.debugScene("research10"); check(g.resCraft == kNightjar, "current research10 selects Nightjar");
    g.debugScene("research20"); check(g.resCraft == kMantis, "current research20 selects generic Mantis");
    g.debugScene("research40"); check(g.resCraft == kWraith, "current research40 selects Wraith");
    g.debugScene("research15"); check(g.resCraft == kPeregrine, "research15 selects the Kestrel");
    std::printf("FX-27 Gatling game: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
  }
};
int main() { return GameTest::run(); }
