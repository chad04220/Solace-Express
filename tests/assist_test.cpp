// Assisted flying and time warp, on every aircraft, through the real game loop and its key bindings: the keys roll and
// pitch it, let go it holds its bank and its flight path (wings level from a shallow bank), and a warp to x16 under the
// pilot's hands stays steady - and answers the keys without running away. (The XR-30 and XR-40 keep their own
// fly-by-wire, which takes rate commands already: they fly assisted only in the warp.) The warp drops back to x1 by itself low over
// the ground and near a checkpoint.
#include "../src/game.h"
#include "test_world.h"
#include <cstdio>

struct GameTest {
  static int run() {
    buildTestWorld(); buildStory(); g_audio.init(48000);
    int fails = 0;
    // open sea west of the islands, well inside the chart
    vec3 sea(-33000.f, 0.f, 2000.f);
    if (g_world.height(sea.x, sea.z) > 0.f) sea = vec3(33000.f, 0.f, -2000.f);
    const float alt = std::max(g_world.height(sea.x, sea.z), 0.f) + 1600.f;
    for (int spec = 0; spec < kAircraftCount; spec++) {
      Game g; g.initHeadless(); g.set.traffic = false; g.failuresArmed = false; g.set.assist = 1;
      Contract c; c.id = "ASSIST"; c.title = "Assist"; c.type = CT_FERRY; c.startAirborne = true;
      c.from = g_world.findAirport("MDB"); c.to = g_world.findAirport("FAR");
      c.wx = Weather(); c.wx.windSpeed = 2.f; c.wx.gust = 0.f; c.wx.turbulence = 0.05f; c.wx.cloudCover = 0.1f; c.wx.timeOfDay = 12.f;
      g.career.license = LIC_ATP;
      g.startFlight(c, spec, Career::SRC_RENT);
      const AircraftSpec& s = kAircraft[spec];
      g.plane.reset(&s, vec3(sea.x, alt, sea.z), 90.f, s.maxFuel, 100.f, true, s.cruise);
      g.takeoffAnnounced = true; g.engineAutoStarted = true; g.screen = SCR_FLIGHT; g.paused = false;
      g.plane.ctl.throttle = s.special ? 0.55f : 0.75f;
      const float dt = 1.f / 60.f;
      auto fly = [&](float seconds, int action = -1) {
        const int key = action >= 0 ? g.set.keyBind[action] : 0;
        for (int i = 0, n = (int)(seconds / dt); i < n && g.screen == SCR_FLIGHT && !g.crashed; i++) {
          if (key) g.in.down[key] = true;
          g.update(dt);
          g.in.endFrame();
        }
        if (key) g.in.down[key] = false;
      };
      auto bank = [&] { return g.plane.bankDeg(); };
      fly(3.f);
      const bool assisted = g.plane.assistOn;
      float b1 = 0, bankDev = 0, altDev = 0, level = 0, vsDev = 0, stall = 0;
      if (!s.special) {
      // roll in, let go: it keeps the bank, and the height
      fly(0.8f, ACT_ROLL_R); fly(2.f);
      b1 = bank(); const float a1 = g.plane.pos.y;
      for (int i = 0; i < 15; i++) { fly(1.f); bankDev = std::max(bankDev, fabsf(bank() - b1)); altDev = std::max(altDev, fabsf(g.plane.pos.y - a1)); }
      // roll out to a shallow bank, let go: wings level
      for (int i = 0; i < 300 && bank() > 4.f; i++) fly(dt, ACT_ROLL_L);
      fly(8.f);
      level = fabsf(bank());
      // pitch up, let go: the climb it was given is held, clear of the stall
      fly(0.5f, ACT_PITCH_UP); fly(2.f);
      const float vs1 = g.plane.vel.y;
      for (int i = 0; i < 8; i++) { fly(1.f); vsDev = std::max(vsDev, fabsf(g.plane.vel.y - vs1)); stall = std::max(stall, g.plane.stallWarn); }
      fly(0.5f, ACT_PITCH_DN); fly(3.f);
      }
      // time warp x16 under the pilot's hands: steady, and a roll at x16 is held where it was let go
      const float vsW = g.plane.vel.y;
      g.timeAccel = 16.f;
      fly(4.f);
      const bool warped = g.timeAccel == 16.f;
      const float warpLevel = fabsf(bank()), warpAlt = fabsf(g.plane.vel.y - vsW);
      fly(0.25f, ACT_ROLL_R); fly(1.5f);
      const float bw = bank(); float warpDev = 0;
      for (int i = 0; i < 3; i++) { fly(1.f); warpDev = std::max(warpDev, fabsf(bank() - bw)); }
      const bool alive = g.screen == SCR_FLIGHT && !g.crashed;
      const bool civil = !s.special;
      const bool ok = assisted == civil && alive && (!civil || (fabsf(b1) > 12.f && bankDev < 6.f && altDev < 90.f && level < 3.f && vsDev < 2.5f && stall < 0.8f))
                      && warped && warpLevel < 5.f && warpAlt < 2.5f && warpDev < 8.f;
      printf("%-18s assisted %d: bank %5.1f held to %4.1f deg, height to %4.0f m; level %4.1f deg; climb held to %4.2f m/s (stall %.2f); x16 warp %d: bank %4.1f, climb to %4.2f m/s, rolled %5.1f held to %4.1f: %s\n",
             s.name, assisted, b1, bankDev, altDev, level, vsDev, stall, warped, warpLevel, warpAlt, bw, warpDev, ok ? "ok" : "FAIL");
      fails += !ok;
    }
    {   // the warp's drop-outs: low over the ground, then a checkpoint ahead; and the T key's steps
      Game g; g.initHeadless(); g.set.traffic = false; g.failuresArmed = false;
      Contract c; c.id = "WARP"; c.title = "Warp"; c.type = CT_FERRY; c.startAirborne = true;
      c.from = g_world.findAirport("MDB"); c.to = g_world.findAirport("FAR"); c.wx = Weather(); c.wx.windSpeed = 0; c.wx.turbulence = 0;
      g.startFlight(c, 1, Career::SRC_RENT);
      const AircraftSpec& s = kAircraft[1];
      g.plane.reset(&s, vec3(sea.x, std::max(g_world.height(sea.x, sea.z), 0.f) + 1600.f, sea.z), 90.f, s.maxFuel, 100.f, true, s.cruise);
      g.takeoffAnnounced = true; g.engineAutoStarted = true; g.screen = SCR_FLIGHT; g.plane.ctl.throttle = 0.75f;
      const int T = g.set.keyBind[ACT_TIME];
      for (int i = 0; i < 30; i++) g.update(1.f / 60.f);   // (into the flight: a key held across the change of context is set aside)
      float steps[5] = {};
      for (int i = 0; i < 5; i++) { g.in.down[T] = g.in.pressed[T] = true; g.update(1.f / 60.f); g.in.endFrame(); g.in.down[T] = false; g.update(1.f / 60.f); steps[i] = g.timeAccel; }
      const bool keys = steps[0] == 2.f && steps[1] == 4.f && steps[2] == 8.f && steps[3] == 16.f && steps[4] == 1.f;
      g.timeAccel = 16.f; g.plane.pos.y = std::max(g_world.height(g.plane.pos.x, g.plane.pos.z), 0.f) + 180.f; g.update(1.f / 60.f);
      const bool low = g.timeAccel == 1.f;
      bool told = false; for (auto& t : g.toasts) told |= t.text.find("Time warp off") != std::string::npos;
      g.plane.pos.y = std::max(g_world.height(g.plane.pos.x, g.plane.pos.z), 0.f) + 1600.f;
      g.contract.wps.push_back({g.plane.pos.x + 900.f, g.plane.pos.z, g.plane.pos.y}); g.wpIndex = 0;
      g.timeAccel = 16.f; g.update(1.f / 60.f);
      const bool gate = g.timeAccel == 1.f;
      const bool ok = keys && low && told && gate;
      printf("Time warp: T steps x2 x4 x8 x16 x1 %d, off low over the ground %d (said so %d), off before a checkpoint %d: %s\n", keys, low, told, gate, ok ? "ok" : "FAIL");
      fails += !ok;
    }
    printf("%d failures\n", fails);
    return fails ? 1 : 0;
  }
};

int main() { return GameTest::run(); }
