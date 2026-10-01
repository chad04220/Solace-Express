// End-to-end gameplay test: a scripted pilot flies Lesson 1 and then lands at a destination,
// exercising the real game loop (completion detection, scoring, payout, story progression).
#include "../src/game.h"
#include <cstdlib>
// attitude-based vertical-speed controller (same structure as the in-game autopilot)
static float s_pI = 0;
static void pitchFor(Plane& p, float vsT, float dt, float maxPitch = 14.f) {
  s_pI = clampf(s_pI + (vsT - p.vel.y) * 0.15f * dt, -8.f, 12.f);
  float pitchT = clampf(s_pI + 0.8f * (vsT - p.vel.y), -10.f, maxPitch);
  p.ctl.pitch = clampf(0.07f * (pitchT - p.pitchDeg()) - 1.2f * p.w.x, -1, 1);
}
struct GameTest {
  static int run() {
    g_world.build(); buildStory();
    g_audio.init(48000);
    Game g; g.initHeadless(); g.botControl = true;
    int fails = 0;
    // ---- Lesson 1: takeoff and fly through the rings
    g.startFlight(g_story[0], 0, Career::SRC_LESSON);
    const float dt = 1.f / 60.f;
    float t = 0;
    for (; t < 400 && g.screen == SCR_FLIGHT; t += dt) {
      Plane& p = g.plane;
      p.ctl.brake = t < 3 ? 1.f : 0.f;
      p.ctl.throttle = t < 3 ? 0.f : 1.f;
      // steer toward the next ring
      vec3 tgt = g.wpIndex < (int)g.contract.wps.size() ? vec3(g.contract.wps[g.wpIndex].x, g.contract.wps[g.wpIndex].alt, g.contract.wps[g.wpIndex].z) : p.pos + p.forward() * 100.f;
      vec3 to = tgt - p.pos;
      float brg = atan2f(to.x, -to.z) / DEG;
      float herr = wrapAngle((brg - p.heading()) * DEG) / DEG;
      if (p.onGround) { p.ctl.yaw = clampf(herr * 0.1f, -1, 1); p.ctl.roll = 0; p.ctl.pitch = p.ias > p.spec->vr ? 0.6f : 0.f; }
      else {
        float bankT = clampf(herr * 1.5f, -25, 25);
        p.ctl.roll = clampf((bankT - p.bankDeg()) * 0.05f + p.w.z * 0.3f, -1, 1);
        float vsT = clampf((tgt.y - p.pos.y) * 0.08f, -3, 4);
        if (p.ias < p.spec->vr + 4) vsT = std::min(vsT, 1.f);
        pitchFor(p, vsT, dt);
        p.ctl.yaw = 0;
      }
      g.update(dt);
      if (getenv("TRACE") && fmodf(t, 3.f) < dt) printf("  t%3.0f gnd %d ias %5.1f agl %6.1f hdg %5.1f pitch %5.1f bank %5.1f vs %5.1f thr %.2f rpm %4.0f run %d\n", t, p.onGround, p.ias, p.agl(), p.heading(), p.pitchDeg(), p.bankDeg(), p.vel.y, p.ctl.throttle, p.rpm, p.engineRunning);
    }
    printf("Lesson 1: screen=%d success=%d wp=%d/%zu t=%.0fs story=%d money=%d %s\n", g.screen, g.lastSuccess, g.wpIndex, g.contract.wps.size(), t, g.career.storyIndex, g.career.money, g.debriefTitle.c_str());
    if (!(g.screen == SCR_DEBRIEF && g.lastSuccess && g.career.storyIndex == 1)) fails++;

    // ---- Approach and landing at Orchard Valley (contract C1 style), Wren rental
    g.career.license = LIC_PPL; g.career.storyIndex = 4; g.career.location = g_world.findAirport("ORC");
    int money0 = g.career.money;
    Contract c = g_story[4];  // C1 ORC -> MDB
    c.wx.windSpeed = 2; c.wx.gust = 0; c.wx.turbulence = 0.05f;
    g.startFlight(c, 1, Career::SRC_RENT);
    const Airport& a = g_world.airports[c.to];
    vec3 dir = a.dir();
    vec3 thr = a.threshold(false);
    vec3 start = thr - dir * 3500.f; start.y = a.elev + 3500.f * tanf(3.f * DEG) + 15.f;
    g.plane.reset(&kAircraft[1], start, a.heading, 60, 150, true, kAircraft[1].vref + 6);
    g.takeoffAnnounced = true; g.engineAutoStarted = true;
    g.plane.ctl.flaps = 1.f; g.flapNotch = 1.f; s_pI = -2.f;
    float tdFpm = 0;
    for (t = 0; t < 300 && g.screen == SCR_FLIGHT; t += dt) {
      Plane& p = g.plane;
      vec3 rel = p.pos - thr;
      float along = dot(vec3(rel.x, 0, rel.z), dir);          // negative before threshold
      float lat = dot(vec3(rel.x, 0, rel.z), vec3(-dir.z, 0, dir.x));
      float agl = p.pos.y - a.elev;
      if (!p.onGround && !g.touchedDown) {
        float ideal = a.elev + std::max(0.f, (-along + 250.f)) * tanf(3.f * DEG);
        float hdgT = a.heading - clampf(lat * 0.08f, -20, 20);
        float herr = wrapAngle((hdgT - p.heading()) * DEG) / DEG;
        p.ctl.roll = clampf((clampf(herr * 2.f, -15, 15) - p.bankDeg()) * 0.05f + p.w.z * 0.3f, -1, 1);
        float vsT = agl < 7.f ? -0.7f : clampf(-p.ias * tanf(3.f * DEG) + (ideal - p.pos.y) * 0.15f, -6, 1);
        pitchFor(p, vsT, dt);
        p.ctl.throttle = agl < 6.f ? 0.f : clampf(0.35f + (p.spec->vref - p.ias) * 0.05f, 0, 1);
        p.ctl.yaw = clampf(p.beta * 3.f, -1, 1);
      } else { p.ctl.throttle = 0; p.ctl.brake = 1; p.ctl.pitch = 0; p.ctl.roll = 0; p.ctl.yaw = 0; }
      g.update(dt);
      if (getenv("TRACE") && fmodf(t, 0.5f) < dt && t < 14) printf("  t%4.1f gnd %d ias %5.1f agl %6.1f along %6.0f lat %5.1f pitch %5.1f vs %5.1f thr %.2f bank %5.1f ctlP %5.2f ctlR %5.2f flap %.2f alpha %5.1f\n", t, p.onGround, p.ias, agl, along, lat, p.pitchDeg(), p.vel.y, p.ctl.throttle, p.bankDeg(), p.ctl.pitch, p.ctl.roll, p.flaps, p.alpha/DEG);
      if (g.touchedDown && tdFpm == 0) tdFpm = g.touchdownFpm;
    }
    int total = g.career.money - money0;
    printf("Landing: screen=%d success=%d touchdown=%.0f fpm  stars=%d  money %+d  location=%s  %s\n", g.screen, g.lastSuccess, tdFpm, g.stars, total,
           g_world.airports[g.career.location].code, g.debriefTitle.c_str());
    for (auto& l : g.payout) printf("   %-30s %d\n", l.label.c_str(), l.amount);
    if (!(g.screen == SCR_DEBRIEF && g.lastSuccess && g.career.location == c.to)) fails++;
    printf("%d failures\n", fails);
    return fails;
  }
};
int main() { return GameTest::run(); }
