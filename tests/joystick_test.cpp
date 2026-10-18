// Joysticks, the portable half: each axis's shaping (direction, dead zone, curve; a stick -1..1, a lever 0..1), a
// binding finding its device again (its id, else its product name on another port), and joystick.cfg round-tripping
// - names with spaces, unknown lines and damaged values included
#include "../src/joystick.h"
#include <cmath>
#include <cstdio>
#include <string>
static int fails = 0;
static void check(bool c, const char* what) { if (!c) { printf("  !! %s\n", what); fails++; } }
static bool near(float a, float b) { return std::fabs(a - b) < 1e-4f; }
int main() {
  {   // a stick: nothing in the dead zone, full travel still full, symmetric, monotone; the curve softens the centre only
    AxisBind b; b.dead = .1f;
    check(shapeAxis(.05f, b, false) == 0.f && shapeAxis(-.1f, b, false) == 0.f, "inside the dead zone reads zero");
    check(near(shapeAxis(1.f, b, false), 1.f) && near(shapeAxis(-1.f, b, false), -1.f), "full travel reads full");
    check(near(shapeAxis(.55f, b, false), .5f), "the rest of the travel is rescaled (half way out of the dead zone reads half)");
    check(near(shapeAxis(-.3f, b, false), -shapeAxis(.3f, b, false)), "symmetric");
    bool mono = true; float last = -2.f;
    for (int i = -100; i <= 100; i++) { const float v = shapeAxis(i / 100.f, b, false); mono &= v >= last; last = v; }
    check(mono, "monotone over the whole travel");
    b.curve = 1.f;
    check(shapeAxis(.55f, b, false) < .2f && near(shapeAxis(1.f, b, false), 1.f), "full curve: gentle about the centre, full at the end");
    b.invert = true;
    check(near(shapeAxis(1.f, b, false), -1.f), "reversed");
  }
  {   // a lever: -1..1 becomes 0..1, a dead zone at each end so idle and full are reached
    AxisBind b; b.dead = .05f;
    check(shapeAxis(-1.f, b, true) == 0.f && shapeAxis(-.92f, b, true) == 0.f, "idle reached at the bottom");
    check(near(shapeAxis(1.f, b, true), 1.f) && near(shapeAxis(.92f, b, true), 1.f), "full reached at the top");
    check(near(shapeAxis(0.f, b, true), .5f), "the middle of the travel is half");
    b.invert = true;
    check(near(shapeAxis(-1.f, b, true), 1.f), "a reversed lever reads full at its other end");
    check(shapeAxis(NAN, b, true) == 0.f && shapeAxis(INFINITY, b, false) == 0.f, "a bad reading is no input");
    AxisBind wild; wild.dead = 5.f; wild.curve = -3.f;
    const float v = shapeAxis(.5f, wild, true);
    check(v >= 0.f && v <= 1.f, "out-of-range settings are clamped");
  }
  {   // finding the device: its id first, else its name (moved to another port), else none
    std::vector<JoyState> j(3);
    j[0].id = "A"; j[0].name = "Pedals"; j[1].id = "B"; j[1].name = "Stick"; j[2].id = "C"; j[2].name = "Stick";
    check(joyFind(j, "C", "Stick") == 2, "the same device by its id, though another has its name");
    check(joyFind(j, "Z", "Stick") == 1, "a device on another port by its name");
    check(joyFind(j, "Z", "Throttle") == -1 && joyFind({}, "A", "Pedals") == -1, "absent: none");
  }
  {   // joystick.cfg: what goes in comes back, names with spaces included; unknown lines and ids are skipped
    static const char* ids[] = {"gear", "flapsDown", "fire"};
    AxisBind ax[AX_COUNT]; JoyButtonBind bt[3];
    ax[AX_PITCH] = {"{11-22}", "T.16000M FCS", 1, false, .07f, .3f};
    ax[AX_THROTTLE] = {"{33-44}", "TWCS Throttle", 2, true, .02f, 0.f};
    bt[0] = {"{11-22}", "T.16000M FCS", 4}; bt[2] = {"{55}", "MFG Crosswind V2", 121};
    const std::string text = joyConfigText(ax, bt, ids, 3);
    AxisBind ra[AX_COUNT]; JoyButtonBind rb[3];
    joyConfigParse(text + "axis warp 3 0 0 0 - nothing\nbutton eject 3 - x\ngarbage\naxis roll x y\n", ra, rb, ids, 3);
    check(ra[AX_PITCH].axis == 1 && ra[AX_PITCH].dev == "{11-22}" && ra[AX_PITCH].devName == "T.16000M FCS" && near(ra[AX_PITCH].dead, .07f) && near(ra[AX_PITCH].curve, .3f), "a stick axis round-trips");
    check(ra[AX_THROTTLE].invert && ra[AX_THROTTLE].devName == "TWCS Throttle" && ra[AX_THROTTLE].axis == 2, "a reversed lever round-trips");
    check(!ra[AX_ROLL].bound() && !ra[AX_YAW].bound() && !ra[AX_BRAKE].bound(), "unbound axes stay unbound (and a damaged line changes nothing)");
    check(rb[0].button == 4 && rb[0].devName == "T.16000M FCS" && !rb[1].bound() && rb[2].button == 121 && rb[2].devName == "MFG Crosswind V2", "button bindings round-trip");
    check(joyConfigText(ra, rb, ids, 3) == text, "read back, it writes the same file");
    AxisBind da[AX_COUNT]; JoyButtonBind db[3];
    joyConfigParse("axis pitch 99 1 7 -2 dev name\nbutton gear 500 dev name\n", da, db, ids, 3);
    check(!da[AX_PITCH].bound() && da[AX_PITCH].dead <= .45f && da[AX_PITCH].curve >= 0.f && !db[0].bound(), "damaged values are rejected or clamped");
  }
  printf("Joystick bindings: %s\n", fails ? "FAIL" : "ok");
  return fails ? 1 : 0;
}
