// Air Xpress - XR-11 Wraith systems: the cloak, the retracting laser turrets, the bomb bay and the dark-energy
// weapons out in the world (laser bolts, plasma bombs, detonations and their glassed craters)
#include "game.h"

namespace {
float frand() { return (rand() % 10000) * 0.0001f; }
vec3 rndDir() { vec3 d(frand() - 0.5f, frand() - 0.5f, frand() - 0.5f); return length(d) > 1e-3f ? normalize(d) : vec3(0, 1, 0); }
// laser emitter lens tips (body coords) once the turrets are fully out
const vec3 kLaserLens[2] = {vec3(-0.95f, -0.68f, -6.44f), vec3(0.95f, -0.68f, -6.44f)};
// the bomb in its cradle (body coords)
const vec3 kBayBomb(0.f, -0.31f, 0.1f);
float groundAt(vec3 p) { return std::max(g_world.height(p.x, p.z), 0.f); }
// first point where a segment from a (direction d, length L) meets the ground or the sea; t < 0 when clear
float groundHit(vec3 a, vec3 d, float L) {
  float t = 0, prev = 0;
  while (t < L) {
    float step = t < 300.f ? 6.f : t < 1500.f ? 15.f : 30.f;
    prev = t; t = std::min(t + step, L);
    vec3 p = a + d * t;
    if (p.y < groundAt(p)) {
      float lo = prev, hi = t;
      for (int i = 0; i < 8; i++) { float m = 0.5f * (lo + hi); vec3 q = a + d * m; if (q.y < groundAt(q)) hi = m; else lo = m; }
      return hi;
    }
  }
  return -1.f;
}
}

void Game::wraithControls(float dt) {
  WraithState& W = wraith;
  bool air = !plane.onGround && !crashed;
  // X, or a double tap of gamepad A (A stays the wheel brake): cloak
  bool dblA = false;
  W.padATap += dt;
  if (in.pad && (in.buttonsPressed & PAD_A) && !showMap) {
    if (W.padATap < 0.35f) { dblA = true; W.padATap = 9.f; } else W.padATap = 0;
  }
  if (in.pressed['X'] || dblA) {
    W.cloakOn = !W.cloakOn;
    toast(W.cloakOn ? "CLOAK ENGAGED" : "CLOAK DISENGAGED", vec3(0.75f, 0.45f, 1.f));
    g_audio.trigger(SFX_CLOAK, W.cloakOn ? 1.f : 0.7f);
  }
  // Y (keyboard or gamepad, in the air on the pad): weapons hot - the laser turrets drop out and the bomb bay opens.
  // Y again: weapons safe - bay closed, turrets stowed. While hot the gamepad bumpers are the triggers, not the rudder.
  if (in.pressed['Y'] || (air && (in.buttonsPressed & PAD_Y))) {
    W.armed = !W.armed;
    toast(W.armed ? "WEAPONS HOT - lasers out, bomb bay open  (RB fire, LB bomb)" : "WEAPONS SAFE - bay closed, lasers stowed", vec3(1.f, 0.35f, 0.4f));
    g_audio.trigger(SFX_GEAR_CLUNK, 0.6f);
  }
  // fire: left mouse or Enter held, or the right bumper while weapons are hot
  bool padHot = air && in.pad && W.armed;
  bool fire = (in.mDown[0] && !showMap) || (in.down[K_ENTER] && !showMap) || (padHot && (in.buttons & PAD_RB));
  if (fire && !W.armed) { W.armed = true; g_audio.trigger(SFX_GEAR_CLUNK, 0.6f); }
  W.laserCD -= dt;
  if (fire && W.lasers > 0.97f && W.laserCD <= 0) { fireLaser(); W.laserCD = 0.12f; }
  // bombs: Backspace, middle mouse, or the left bumper while weapons are hot - each press queues a drop
  if (in.pressed[K_BACK] || in.mPressed[2] || (padHot && (in.buttonsPressed & PAD_LB))) {
    if (W.bombQueue < 3) W.bombQueue++;
  }
}

void Game::fireLaser() {
  WraithState& W = wraith;
  int s = W.laserSide; W.laserSide ^= 1;
  vec3 a = plane.pos + plane.q.rotate(kLaserLens[s]);
  vec3 f = plane.forward();
  vec3 aim = plane.pos + f * 650.f;   // the two turrets converge 650 m ahead
  vec3 d = normalize(aim - a);
  const float L = 4500.f;
  float tg = groundHit(a, d, L);
  float tt = 0; int k = traffic.rayHit(a, d, tg > 0 ? tg : L, tt);
  float tHit = k >= 0 ? tt : tg;
  vec3 b = a + d * (tHit > 0 ? tHit : L);
  W.bolts.push_back({a, b, 0.09f});
  W.laserGlow = 1.f;
  g_audio.trigger(SFX_LASER, 0.8f);
  // muzzle flash
  spawn(a, plane.vel, 0.06f, 0.9f, 2.f, vec3(1.f, 0.3f, 0.45f) * 3.f, 1.f, SPR_GLOW, 0.f, 0.f);
  if (k >= 0) {   // a direct hit: the aircraft comes apart in a fireball
    vec3 p = traffic.craft[k].pos, v = traffic.craft[k].vel;
    traffic.craft[k].alive = false;
    W.kills++;
    toast(fmt("SPLASH %d - %s down", W.kills, kAircraft[traffic.craft[k].spec].name), vec3(1.f, 0.5f, 0.3f));
    g_audio.trigger(SFX_BOOM, 0.5f);
    for (int i = 0; i < 40; i++) spawn(p + rndDir() * 2.f, v * 0.5f + rndDir() * (10.f + 25.f * frand()), 0.6f + frand(), 2.5f + 2.f * frand(), 4.f, vec3(1.f, 0.55f, 0.2f) * 3.f, 1.f, SPR_FIRE, 1.2f, 0.4f);
    for (int i = 0; i < 25; i++) spawn(p, v * 0.6f + rndDir() * (30.f + 50.f * frand()), 1.f + frand(), 0.25f, -0.1f, vec3(1.f, 0.7f, 0.3f) * 4.f, 1.f, SPR_SPARK, 0.8f, -1.f);
    for (int i = 0; i < 20; i++) spawn(p + rndDir() * 3.f, v * 0.3f + rndDir() * 6.f, 4.f + 3.f * frand(), 3.f, 5.f, vec3(0.12f), 0.6f, SPR_SMOKE, 1.f, 0.6f);
  } else if (tg > 0) {   // ground or sea: sparks, a brief fire and a puff of smoke or steam
    bool water = b.y < 0.5f;
    for (int i = 0; i < 14; i++) spawn(b + vec3(0, 0.3f, 0), rndDir() * (8.f + 18.f * frand()) + vec3(0, 6.f, 0), 0.3f + 0.4f * frand(), 0.12f, -0.1f, vec3(1.f, 0.4f, 0.5f) * 4.f, 1.f, SPR_SPARK, 1.f, -1.f);
    spawn(b + vec3(0, 0.5f, 0), vec3(0, 1.f, 0), 0.15f, 3.f, 3.f, vec3(1.f, 0.3f, 0.45f) * 3.f, 1.f, SPR_GLOW, 0.f, 0.f);
    spawn(b + vec3(0, 1.f, 0), vec3(0, 2.f, 0), 2.5f, 1.5f, 3.f, water ? vec3(0.85f, 0.88f, 0.9f) : vec3(0.15f, 0.13f, 0.12f), 0.5f, SPR_SMOKE, 1.f, 0.5f);
    if (!water && frand() < 0.4f) spawn(b, vec3(0, 1.f, 0), 0.8f, 1.2f, 1.5f, vec3(1.f, 0.5f, 0.2f) * 2.f, 1.f, SPR_FIRE, 1.f, 0.2f);
  }
  if (ufo.on && ufo.t < 20.f) {   // tag the UFO and it decides it has seen enough
    vec3 rel = ufo.pos - a; float along = dot(rel, d);
    if (along > 0 && along < (tHit > 0 ? tHit : L) && length(rel - d * along) < 9.f) { ufo.t = 23.f; toast("The visitors don't appreciate that...", vec3(0.4f, 1.f, 0.6f)); }
  }
}

void Game::detonate(vec3 p, bool water) {
  WraithState& W = wraith;
  W.blasts.push_back({p, water ? 80.f : 100.f, 0.f, 3.6f, water});
  if (W.blasts.size() > 6) W.blasts.erase(W.blasts.begin());
  if (!water) {
    W.craters.push_back({p.x, p.z, 24.f, -8.f});
    if (W.craters.size() > 7) W.craters.erase(W.craters.begin());
  }
  float dCam = length(p - camPos);
  g_audio.trigger(SFX_PLASMA, clampf(1.3f - dCam / 6000.f, 0.25f, 1.f));
  // everything inside the fireball goes with it
  std::vector<vec3> where;
  int n = traffic.destroyNear(p, 230.f, where);
  if (n > 0) { W.kills += n; toast(fmt("%d aircraft caught in the blast", n), vec3(1.f, 0.5f, 0.3f)); }
  for (vec3 q : where)
    for (int i = 0; i < 20; i++) spawn(q + rndDir() * 2.f, rndDir() * (10.f + 25.f * frand()), 0.6f + frand(), 2.5f + 2.f * frand(), 4.f, vec3(1.f, 0.55f, 0.2f) * 3.f, 1.f, SPR_FIRE, 1.2f, 0.4f);
  // particles: violet sparks, a column of black smoke (or a tower of spray), a flat shock ring
  for (int i = 0; i < 160; i++) { vec3 d = rndDir(); d.y = fabsf(d.y) * 0.8f + 0.1f; spawn(p + vec3(0, 2.f, 0), normalize(d) * (40.f + 110.f * frand()), 1.2f + 1.5f * frand(), 1.2f, -0.2f, lerp(vec3(0.6f, 0.2f, 1.f), vec3(0.4f, 0.8f, 1.f), frand()) * 5.f, 1.f, SPR_SPARK, 0.5f, -1.f); }
  for (int i = 0; i < 45; i++) {
    vec3 d = rndDir(); d.y = fabsf(d.y);
    spawn(p + d * 10.f, d * (6.f + 12.f * frand()) + vec3(0, 8.f + 10.f * frand(), 0), 12.f + 8.f * frand(), 14.f + 8.f * frand(), 14.f,
          water ? vec3(0.85f, 0.9f, 0.95f) : vec3(0.06f, 0.05f, 0.08f), water ? 0.55f : 0.75f, SPR_SMOKE, 0.6f, water ? 0.f : 0.5f);
  }
  vec3 ax(30.f, 0, 0), ay(0, 0, 30.f);
  bursts.push_back({p + vec3(0, 2.f, 0), ax, ay, vec3(0.7f, 0.35f, 1.f), 0.f});
  // the shock reaches the player: a jolt that scales with how close it was
  float dp = length(p - plane.pos);
  if (dp < 600.f && !crashed) {
    vec3 push = normalize(plane.pos - p + vec3(0, 1.f, 0)) * (25.f * (1.f - dp / 600.f));
    plane.vel += push;
    plane.w += vec3(frand() - 0.5f, frand() - 0.5f, frand() - 0.5f) * (1.5f * (1.f - dp / 600.f));
  }
}

void Game::updateWraith(float dt) {
  WraithState& W = wraith;
  bool wr = plane.spec && plane.spec->special == 2 && !crashed;
  // cloak: the field spreads from the nose to the tail (and collapses from the tail back)
  float tgt = wr && W.cloakOn ? 1.f : 0.f;
  W.stealth = clampf(W.stealth + (tgt > W.stealth ? 0.8f : -1.1f) * dt, 0.f, 1.f);
  if (fabsf(W.stealth - tgt) < 1e-4f) W.stealth = tgt;
  W.front = -9.5f + 19.f * W.stealth;
  // turrets
  W.lasers = clampf(W.lasers + ((wr && W.armed) ? 1.f : -1.f) * dt / 0.7f, 0.f, 1.f);
  W.laserGlow = std::max(0.f, W.laserGlow - dt * 9.f);
  // bomb bay: open for queued drops, release when the doors are clear, a new bomb condenses in the cradle
  if (!wr) W.bombQueue = 0;
  bool wantOpen = W.bombQueue > 0 || W.bayHold > 0 || (wr && W.armed && !plane.onGround);   // weapons hot: bay stays open
  W.bay = clampf(W.bay + (wantOpen ? 3.f : -2.f) * dt, 0.f, 1.f);
  W.bayHold = std::max(0.f, W.bayHold - dt);
  W.bombLoaded = std::min(1.f, W.bombLoaded + dt / 0.8f);
  if (W.bombQueue > 0 && W.bay > 0.95f && W.bombLoaded >= 1.f && !plane.onGround) {
    vec3 p = plane.pos + plane.q.rotate(kBayBomb - vec3(0, 0.25f, 0));
    W.bombs.push_back({p, plane.vel + plane.up() * -3.f, 0.f});
    W.bombLoaded = 0; W.bombQueue--; W.bayHold = 0.6f;
    g_audio.trigger(SFX_GEAR_CLUNK, 0.6f);
    toast("PLASMA BOMB AWAY", vec3(0.7f, 0.4f, 1.f));
  }
  if (W.bombQueue > 0 && plane.onGround) W.bombQueue = 0;
  // bolts fade
  for (auto& b : W.bolts) b.life -= dt;
  W.bolts.erase(std::remove_if(W.bolts.begin(), W.bolts.end(), [](const WraithState::Bolt& b) { return b.life <= 0; }), W.bolts.end());
  // bombs fall (a little drag), trail violet sparks, and go off on the ground, the sea or near an aircraft
  for (size_t i = 0; i < W.bombs.size(); i++) {
    WraithState::Bomb& b = W.bombs[i];
    b.t += dt;
    b.v += vec3(0, -G0, 0) * dt - b.v * (0.004f * length(b.v) * dt * 0.1f);
    b.p += b.v * dt;
    if (frand() < 0.6f) spawn(b.p, b.v * 0.9f + rndDir() * 3.f, 0.4f, 0.25f, 0.5f, vec3(0.6f, 0.25f, 1.f) * 3.f, 1.f, SPR_SPARK, 2.f, 0.f);
    float g = groundAt(b.p);
    bool near = false; float tt;
    if (traffic.rayHit(b.p, vec3(0, -1, 0), 1.f, tt) >= 0) near = true;
    for (auto& c : traffic.craft) if (c.alive && c.role != TrafficCraft::ESCORT && length(c.pos - b.p) < 25.f) near = true;
    if (b.p.y <= g || near || b.t > 60.f) {
      vec3 at = b.p; if (at.y < g) at.y = g;
      detonate(at, g_world.height(at.x, at.z) < 0.3f && at.y < 1.f);
      W.bombs.erase(W.bombs.begin() + i); i--;
    }
  }
  for (auto& bl : W.blasts) bl.age += dt / bl.dur;
  W.blasts.erase(std::remove_if(W.blasts.begin(), W.blasts.end(), [](const WraithState::Blast& b) { return b.age >= 1.f; }), W.blasts.end());
}

void Game::wraithVisual(FrameParams& fp) {
  const WraithState& W = wraith;
  PlaneVisual& pv = fp.plane;
  if (plane.spec && plane.spec->special == 2 && pv.on) {
    for (int i = 0; i < 4; i++) { pv.wr[0][i] = plane.podTilt[i]; pv.wr[1][i] = plane.podYaw[i]; pv.wr[2][i] = plane.engineRunning ? plane.podThr[i] : 0.f; pv.wr[3][i] = plane.podVane[i]; }
    if (plane.onGround && !plane.engineRunning) for (int i = 0; i < 4; i++) pv.wr[0][i] = plane.nozzle * 0.5f * PI;
    pv.wr[4][0] = fmodf(plane.fanAngle, 6.2832f * 8.f); pv.wr[4][1] = W.bay; pv.wr[4][2] = W.lasers; pv.wr[4][3] = W.stealth;
    pv.wr[5][0] = plane.surf.x; pv.wr[5][1] = plane.surf.y; pv.wr[5][2] = plane.surf.z; pv.wr[5][3] = W.laserGlow;
    pv.wr[6][0] = W.bombLoaded; pv.wr[6][1] = W.front; pv.wr[6][2] = W.armed ? 1.f : 0.f;
  }
  FxVisual& fx = fp.fx;
  fx.beams = 0;
  for (int i = (int)W.bolts.size() - 1; i >= 0 && fx.beams < 2; i--) {
    const WraithState::Bolt& b = W.bolts[i];
    float k = clampf(b.life / 0.09f, 0.f, 1.f);
    float* A = fx.beamA[fx.beams]; float* B = fx.beamB[fx.beams];
    A[0] = b.a.x; A[1] = b.a.y; A[2] = b.a.z; A[3] = 0.22f;
    B[0] = b.b.x; B[1] = b.b.y; B[2] = b.b.z; B[3] = k;
    fx.beams++;
  }
  fx.bombs = 0;
  for (size_t i = 0; i < W.bombs.size() && fx.bombs < 8; i++) { float* o = fx.bomb[fx.bombs++]; o[0] = W.bombs[i].p.x; o[1] = W.bombs[i].p.y; o[2] = W.bombs[i].p.z; o[3] = 0.29f; }
  fx.blasts = 0;
  for (size_t i = 0; i < W.blasts.size() && fx.blasts < 6; i++) {
    const WraithState::Blast& b = W.blasts[i];
    float e = 1.f - (1.f - b.age) * (1.f - b.age) * (1.f - b.age);
    float* o = fx.blast[fx.blasts]; float* I = fx.blastI[fx.blasts]; fx.blasts++;
    o[0] = b.p.x; o[1] = b.p.y + b.R * 0.25f * e; o[2] = b.p.z; o[3] = b.R * (0.15f + 0.85f * e);
    I[0] = b.age; I[1] = 1.f; I[2] = 0; I[3] = 0;
  }
  // glassed craters join the crash crater (if any)
  for (size_t i = 0; i < W.craters.size() && fp.wreck.craterN < 8; i++) {
    float* c = fp.wreck.crater[fp.wreck.craterN++];
    c[0] = W.craters[i].x; c[1] = W.craters[i].z; c[2] = W.craters[i].R; c[3] = W.craters[i].D;
  }
  // a young detonation lights up its surroundings (borrowing the exhaust light)
  for (const auto& b : W.blasts)
    if (b.age < 0.5f) { fp.flameLightPos = b.p + vec3(0, b.R * 0.4f, 0); fp.flameLight = vec3(0.6f, 0.25f, 1.f) * (30000.f * (1.f - b.age * 2.f)); break; }
}
