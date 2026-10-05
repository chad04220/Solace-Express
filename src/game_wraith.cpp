// Solace Express - XR-11 Wraith systems: the cloak, the retracting laser turrets, the bomb bay and the dark-energy
// weapons out in the world (laser bolts, plasma bombs, detonations and their glassed craters)
#include "game.h"
#include "entities.h"

namespace {
float frand() { return (rand() % 10000) * 0.0001f; }
vec3 rndDir() { vec3 d(frand() - 0.5f, frand() - 0.5f, frand() - 0.5f); return length(d) > 1e-3f ? normalize(d) : vec3(0, 1, 0); }
// laser emitter lens tips (body coords) once the turrets are fully out
const vec3 kLaserLens[2] = {vec3(-0.95f, -0.68f, -6.44f), vec3(0.95f, -0.68f, -6.44f)};
const float kBoltSpeed = 2000.f, kBoltStreak = 55.f, kBoltRange = 4500.f;   // muzzle speed (added to the craft's), streak, range
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
  if (actPadP(ACT_BRAKE) && !showMap) {
    if (W.padATap < 0.35f) { dblA = true; W.padATap = 9.f; } else W.padATap = 0;
  }
  if (actPressed(ACT_CLOAK) || dblA) {
    W.cloakOn = !W.cloakOn;
    toast(W.cloakOn ? "CLOAK ENGAGED" : "CLOAK DISENGAGED", vec3(0.75f, 0.45f, 1.f));
    g_audio.trigger(SFX_CLOAK, W.cloakOn ? 1.f : 0.7f);
  }
  // Y (keyboard or gamepad, in the air on the pad): weapons hot - the laser turrets drop out and the bomb bay opens.
  // Y again: weapons safe - bay closed, turrets stowed. While hot the gamepad bumpers are the triggers, not the rudder.
  if (actKeyP(ACT_WEAPONS) || (air && actPadP(ACT_WEAPONS))) {
    W.armed = !W.armed;
    toast(W.armed ? "WEAPONS HOT - lasers out, bomb bay open" : "WEAPONS SAFE - bay closed, lasers stowed", vec3(1.f, 0.35f, 0.4f));
    g_audio.trigger(SFX_GEAR_CLUNK, 0.6f);
  }
  // fire: left mouse or Enter held, or the right bumper while weapons are hot
  bool padHot = air && in.pad && W.armed;
  bool fire = (in.mDown[0] && !showMap) || (actKey(ACT_FIRE) && !showMap) || (padHot && actPad(ACT_FIRE));
  if (W.fireLatch) { if (!in.mDown[0] && !actKey(ACT_FIRE)) W.fireLatch = false; fire = false; }
  if (fire && !W.armed) { W.armed = true; g_audio.trigger(SFX_GEAR_CLUNK, 0.6f); }
  W.wantFire = fire;   // the bolt leaves the lens after this frame's physics step (updateWraith)
  // bombs: Backspace, middle mouse, or the left bumper while weapons are hot - each press queues a drop
  if (actKeyP(ACT_BOMB) || in.mPressed[2] || (padHot && actPadP(ACT_BOMB))) {
    if (W.bombQueue < 3) W.bombQueue++;
  }
}

// A bolt leaves the lens tip at the muzzle speed plus the craft's own velocity, so in the craft's frame it always
// streaks away from the turret, however fast the Wraith is flying.
void Game::fireLaser() {
  WraithState& W = wraith;
  int s = W.laserSide; W.laserSide ^= 1;
  vec3 a = plane.pos + plane.q.rotate(kLaserLens[s]);
  vec3 aim = plane.pos + plane.forward() * 650.f;   // the two turrets converge 650 m ahead
  vec3 d = normalize(aim - a);
  W.bolts.push_back({a, plane.vel + d * kBoltSpeed, d, 0.f, 0.f, kBoltRange / kBoltSpeed, false});
  W.laserGlow = 1.f;
  g_audio.trigger(SFX_LASER, 0.8f);
  spawn(a, plane.vel, 0.06f, 0.9f, 2.f, vec3(1.f, 0.3f, 0.45f) * 3.f, 1.f, SPR_GLOW, 0.f, 0.f);   // muzzle flash
}

// laser scorch craters: a new hit close to an old one deepens and widens it instead of adding another
void Game::addScorch(float x, float z, float R, float D) {
  WraithState& W = wraith;
  for (auto& c : W.scorch)
    if ((c.x - x) * (c.x - x) + (c.z - z) * (c.z - z) < c.R * c.R * 0.6f) { c.R = std::min(std::max(c.R, R) + 0.1f, std::max(R, 2.6f)); c.D = std::min(c.D + 0.1f, std::max(D, 1.f)); return; }
  W.scorch.push_back({x, z, R, D});
  if (W.scorch.size() > 16) W.scorch.erase(W.scorch.begin());
}

// a bolt striking something: aircraft come apart; trees, rocks and buildings take damage and are destroyed in fire
// and smoke when it's enough; the ground is scorched with a small crater
void Game::laserImpact(vec3 b, int craft, int entKind, const Ent* ent) {
  WraithState& W = wraith;
  if (craft >= 0) {
    vec3 p = traffic.craft[craft].pos, v = traffic.craft[craft].vel;
    traffic.craft[craft].alive = false;
    W.kills++;
    toast(fmt("SPLASH %d - %s down", W.kills, kAircraft[traffic.craft[craft].spec].name), vec3(1.f, 0.5f, 0.3f));
    g_audio.trigger(SFX_BOOM, 0.5f);
    for (int i = 0; i < 40; i++) spawn(p + rndDir() * 2.f, v * 0.5f + rndDir() * (10.f + 25.f * frand()), 0.6f + frand(), 2.5f + 2.f * frand(), 4.f, vec3(1.f, 0.55f, 0.2f) * 3.f, 1.f, SPR_FIRE, 1.2f, 0.4f);
    for (int i = 0; i < 25; i++) spawn(p, v * 0.6f + rndDir() * (30.f + 50.f * frand()), 1.f + frand(), 0.25f, -0.1f, vec3(1.f, 0.7f, 0.3f) * 4.f, 1.f, SPR_SPARK, 0.8f, -1.f);
    for (int i = 0; i < 20; i++) spawn(p + rndDir() * 3.f, v * 0.3f + rndDir() * 6.f, 4.f + 3.f * frand(), 3.f, 5.f, vec3(0.12f), 0.6f, SPR_SMOKE, 1.f, 0.6f);
    return;
  }
  bool water = !entKind && b.y < 0.5f && g_world.height(b.x, b.z) < 0.3f;
  // every hit: sparks, a flash and a puff
  for (int i = 0; i < 14; i++) spawn(b + vec3(0, 0.3f, 0), rndDir() * (8.f + 18.f * frand()) + vec3(0, 6.f, 0), 0.3f + 0.4f * frand(), 0.12f, -0.1f, vec3(1.f, 0.4f, 0.5f) * 4.f, 1.f, SPR_SPARK, 1.f, -1.f);
  spawn(b + vec3(0, 0.5f, 0), vec3(0, 1.f, 0), 0.15f, 3.f, 3.f, vec3(1.f, 0.3f, 0.45f) * 3.f, 1.f, SPR_GLOW, 0.f, 0.f);
  spawn(b + vec3(0, 1.f, 0), vec3(0, 2.f, 0), 2.5f, 1.5f, 3.f, water ? vec3(0.85f, 0.88f, 0.9f) : vec3(0.15f, 0.13f, 0.12f), 0.5f, SPR_SMOKE, 1.f, 0.5f);
  if (entKind && ent) {
    int k = entKind - 1, cl = entClass(k);
    const EntKindInfo& I = kEntInfo[k];
    float R = std::max(I.hx * ent->sx, I.hz * ent->sz), H = I.h * ent->sy;
    if (!g_scenery.damage(*ent, k)) {   // damaged, still standing: it burns where it was hit
      spawn(b, vec3(0, 1.5f, 0), 1.2f, 1.6f, 2.f, vec3(1.f, 0.5f, 0.2f) * 2.f, 1.f, SPR_FIRE, 1.f, 0.3f);
      spawn(b + vec3(0, 1.f, 0), vec3(0, 3.f, 0), 4.f, 2.5f, 4.f, vec3(0.1f), 0.6f, SPR_SMOKE, 1.f, 0.5f);
      return;
    }
    // destroyed: a blast sized to what it was, debris, a smoke column and a scorched pit where it stood
    W.wrecked++;
    vec3 base(ent->x, ent->y, ent->z), mid = base + vec3(0, H * 0.4f, 0);
    float sz = std::clamp(R, 1.f, 12.f);
    int nf = cl == EC_BUILDING ? 40 : cl == EC_ROCK ? 18 : 14;
    for (int i = 0; i < nf; i++) spawn(mid + rndDir() * (sz * 0.5f) + vec3(0, H * 0.3f * frand(), 0), rndDir() * (3.f + 6.f * frand()) + vec3(0, 4.f, 0), 0.8f + frand(), sz * (0.5f + 0.5f * frand()), sz * 0.6f,
                                vec3(1.f, 0.5f, 0.2f) * 3.f, 1.f, SPR_FIRE, 1.2f, 0.4f);
    for (int i = 0; i < nf; i++) spawn(mid, rndDir() * (15.f + 30.f * frand()) + vec3(0, 12.f, 0), 1.2f + frand(), 0.3f, -0.1f, cl == EC_TREE ? vec3(1.f, 0.6f, 0.25f) * 3.f : vec3(1.f, 0.75f, 0.4f) * 4.f, 1.f, SPR_SPARK, 1.f, -1.f);
    for (int i = 0; i < nf / 2 + 4; i++) spawn(mid + rndDir() * sz * 0.5f, vec3(0, 3.f + 4.f * frand(), 0) + rndDir() * 2.f, 6.f + 5.f * frand(), sz * 0.8f + 2.f, sz * 0.9f + 3.f,
                                     cl == EC_ROCK ? vec3(0.5f, 0.47f, 0.43f) : vec3(0.09f, 0.08f, 0.08f), 0.7f, SPR_SMOKE, 0.6f, 0.5f);
    g_audio.trigger(SFX_BOOM, clampf(0.25f + R * 0.04f, 0.25f, 0.8f) * clampf(1.4f - length(b - camPos) / 3000.f, 0.2f, 1.f));
    if (ent->y > 0.3f) addScorch(ent->x, ent->z, clampf(R * 0.75f, 1.6f, 9.f), clampf(0.3f + R * 0.08f, 0.4f, 1.4f));
    if (cl == EC_BUILDING) toast(fmt("%s destroyed", I.name), vec3(1.f, 0.6f, 0.3f));
    return;
  }
  if (!water) {
    if (frand() < 0.4f) spawn(b, vec3(0, 1.f, 0), 0.8f, 1.2f, 1.5f, vec3(1.f, 0.5f, 0.2f) * 2.f, 1.f, SPR_FIRE, 1.f, 0.2f);
    for (int i = 0; i < 6; i++) spawn(b, rndDir() * 4.f + vec3(0, 7.f + 5.f * frand(), 0), 1.2f, 0.25f, 0.4f, vec3(0.12f, 0.09f, 0.07f), 0.9f, SPR_SMOKE, 2.f, 0.f);   // thrown dirt
    addScorch(b.x, b.z, 1.6f, 0.5f);
  }
}

// bolts in flight: sweep each one's path this frame against the ground, aircraft, scenery and the UFO (runs after the
// physics step and before this frame's new bolts leave the lenses, so a bolt's first move is the frame after it fires)
void Game::updateBolts(float dt) {
  WraithState& W = wraith;
  for (auto& b : W.bolts) {
    b.age += dt;
    if (b.hit) { b.len -= kBoltSpeed * dt; continue; }   // the streak runs into the impact point
    float segL = length(b.v) * dt;
    vec3 sd = b.v / std::max(length(b.v), 1e-3f), a = b.h;
    float tHit = -1.f; int craft = -1;
    float tg = groundHit(a, sd, segL);
    if (tg >= 0) tHit = tg;
    float tt = 0; int k = traffic.rayHit(a, sd, tHit >= 0 ? tHit : segL, tt);
    if (k >= 0) { tHit = tt; craft = k; }
    int ek = 0; Ent hitEnt; float te = g_scenery.raycast(a, sd, tHit >= 0 ? tHit : segL, &ek, &hitEnt);
    if (te >= 0) { tHit = te; craft = -1; } else ek = 0;
    if (ufo.on && ufo.t < 20.f) {   // tag the UFO and it decides it has seen enough
      vec3 rel = ufo.pos - a; float along = dot(rel, sd);
      if (along > 0 && along < (tHit >= 0 ? tHit : segL) && length(rel - sd * along) < 9.f) { ufo.t = 23.f; toast("The visitors don't appreciate that...", vec3(0.4f, 1.f, 0.6f)); }
    }
    if (tHit >= 0) { b.h = a + sd * tHit; b.hit = true; laserImpact(b.h, craft, ek, ek ? &hitEnt : nullptr); }
    else { b.h = a + b.v * dt; b.len = std::min(kBoltStreak, b.len + kBoltSpeed * dt); }
  }
  W.bolts.erase(std::remove_if(W.bolts.begin(), W.bolts.end(), [](const WraithState::Bolt& b) { return b.age > b.life || (b.hit && b.len <= 0.f); }), W.bolts.end());
}

// The bomb camera is a separate camera object spawned when a bomb leaves the bay. It chases the oldest falling bomb
// from just behind and above, and when that bomb goes off it flies out to a vantage point 400-700 m away and 18
// degrees up, slowly orbiting and pulling back as the cloud climbs. Then it is removed (or picks up the next bomb in the air).
void Game::updateBombCam(float dt) {
  WraithState& W = wraith;
  WraithState::BombCam& C = W.cam;
  if (!C.on) {
    if (W.bombs.empty()) return;
    const WraithState::Bomb& b = W.bombs.front();
    C = WraithState::BombCam();
    C.on = true; C.phase = 0;
    C.pos = b.p - normalize(b.v + vec3(0, -0.1f, 0)) * 18.f + vec3(0, 6.f, 0);
    C.look = b.p; C.tanHalf = 0.3f;
  }
  C.t += dt;
  if (C.phase == 0) {
    if (W.bombs.empty()) { C.on = false; return; }
    const WraithState::Bomb& b = W.bombs.front();
    vec3 vd = normalize(b.v + vec3(0, -0.1f, 0)), side = normalize(cross(vd, vec3(0, 1, 0)) + vec3(1e-4f, 0, 0));
    vec3 want = b.p - vd * 30.f + vec3(0, 8.f, 0) + side * 6.f;
    C.pos = want + (C.pos - want) * expf(-dt * 8.f);
    C.look = b.p + b.v * 0.12f;
    C.tanHalf += (0.28f - C.tanHalf) * (1.f - expf(-dt * 3.f));
  } else {
    C.orbit += dt * 0.14f;
    float rise = clampf(C.t / 7.f, 0.f, 1.f);   // the cloud climbs: pull back and look higher
    float el = 18.f * DEG, d = 420.f + 260.f * rise;
    vec3 want = C.blastP + vec3(sinf(C.orbit) * cosf(el), sinf(el), cosf(C.orbit) * cosf(el)) * d;
    want.y = std::max(want.y, groundAt(want) + 25.f);
    C.pos = want + (C.pos - want) * expf(-dt * 1.8f);   // flies out to the vantage point
    vec3 lookAt = C.blastP + vec3(0, 45.f + 170.f * rise, 0);
    C.look = lookAt + (C.look - lookAt) * expf(-dt * 4.f);
    C.tanHalf += (0.42f - C.tanHalf) * (1.f - expf(-dt * 2.f));
    if (C.t > 6.5f) {   // the blast has burnt out: next bomb in the air, or the camera is removed
      if (!W.bombs.empty()) { C.phase = 0; C.t = 0; }
      else C.on = false;
    }
  }
  C.pos.y = std::max(C.pos.y, groundAt(C.pos) + 3.f);
}

// The research jets' cockpit cameras (feed_cameras.h): game objects riding on the airframe, each at its mount on the
// skin and turned the way its display faces, plus the XR-11's bomb camera, which flies free. They exist while their
// pictures are on show: in the cockpit view.
void Game::buildFeedCameras(FrameParams& fp) {
  fp.feedRig = 0;
  const int rig = plane.spec ? feedRigOf(plane.spec->special) : 0;
  if (!rig || camMode != 1 || crashed || !fp.plane.on) return;
  fp.feedRig = rig;
  const FeedMounts& fm = g_feedMounts[rig];
  if (!fm.ok) return;   // the renderer measures the mounts the first time it sees the craft; the cameras go up next frame
  FeedMount mt[kMaxFeeds];
  // (the displays as sharp as a 1080p screen at the cockpit's field of view, or the window's own when it's larger -
  // up to 1440p; three quarters of that on the lowest quality)
  const float lines = clampf((float)g_ren.H, 1080.f, 1440.f) * (g_ren.quality <= 0 ? 0.75f : 1.f);
  const int n = feedRig(rig, lines * 0.5f / tanf(37.f * DEG), mt);
  const float* R = fp.plane.rot;   // body -> world, column-major
  auto toWorld = [&](vec3 v) { return vec3(R[0] * v.x + R[3] * v.y + R[6] * v.z, R[1] * v.x + R[4] * v.y + R[7] * v.z, R[2] * v.x + R[5] * v.y + R[8] * v.z); };
  const vec3 E(fp.plane.M[22 * 4], fp.plane.M[22 * 4 + 1], fp.plane.M[22 * 4 + 2]);   // the eye (body frame)
  for (int i = 0; i < n; i++) {
    FeedCamera& c = fp.feeds[i];
    c.on = true;
    vec3 lens = mt[i].nose ? fm.nose + vec3(0.f, 0.f, -0.03f) : mt[i].dir * (fm.skin[i] + 0.03f);   // 3 cm off the skin
    c.pos = fp.plane.pos + toWorld(E + lens);
    c.right = toWorld(mt[i].right); c.up = toWorld(mt[i].up); c.back = toWorld(mt[i].back);
    c.tanX = mt[i].tanX; c.tanY = mt[i].tanY; c.pano = mt[i].pano; c.w = mt[i].w; c.h = mt[i].h;
    c.screen = fp.plane.pos + toWorld(E + mt[i].screen); c.screenR = mt[i].screenR;
  }
  if (rig == 2 && fp.fx.feed[3] > 0.5f) {   // the bomb camera, horizon level, framed like the footwell floor pane
    const WraithState::BombCam& C = wraith.cam;
    FeedCamera& c = fp.feeds[kFeedBombSlot];
    vec3 f = normalize(C.look - C.pos), r = normalize(cross(f, vec3(0, 1, 0)) + vec3(1e-4f, 0, 0));
    c.on = true; c.pos = C.pos; c.right = r; c.up = cross(r, f); c.back = -f;
    c.tanY = C.tanHalf; c.tanX = C.tanHalf * mt[9].w / std::max(mt[9].h, 1);
    c.w = mt[9].w; c.h = mt[9].h;
    c.screen = fp.plane.pos + toWorld(E + vec3(0.f, -0.74f, -0.3f)); c.screenR = 0.75f;   // the footwell and seat-side floor panes
  }
}

void Game::detonate(vec3 p, bool water) {
  WraithState& W = wraith;
  W.blasts.push_back({p, water ? 80.f : 100.f, 0.f, 7.f, water});
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
  // ejecta: white-hot glassed fragments arcing out, and clods of earth (or sheets of water) thrown up and falling back
  for (int i = 0; i < 70; i++) {
    vec3 d = rndDir(); d.y = 0.5f + fabsf(d.y);
    spawn(p + vec3(0, 3.f, 0), normalize(d) * (35.f + 70.f * frand()), 2.f + 2.f * frand(), 1.6f, -0.1f,
          lerp(vec3(1.f, 0.75f, 1.f), vec3(0.6f, 0.3f, 1.f), frand()) * 6.f, 1.f, SPR_SPARK, 0.15f, -1.f);
  }
  for (int i = 0; i < 60; i++) {
    vec3 d = rndDir(); d.y = 0.7f + fabsf(d.y);
    spawn(p + vec3(0, 2.f, 0), normalize(d) * (25.f + 55.f * frand()), 3.f + 2.f * frand(), 2.5f + 3.f * frand(), 1.5f,
          water ? vec3(0.8f, 0.85f, 0.9f) : vec3(0.09f, 0.075f, 0.06f), water ? 0.6f : 0.9f, SPR_SMOKE, 0.25f, -1.f);
  }
  // base surge: a ring of dust (or spray) rolling out along the ground under the rising cloud
  for (int i = 0; i < 48; i++) {
    float a = i * (6.2832f / 48.f) + frand() * 0.1f;
    vec3 d(cosf(a), 0, sinf(a));
    vec3 q = p + d * (20.f + 15.f * frand());
    q.y = std::max(groundAt(q), p.y) + 4.f;
    spawn(q, d * (45.f + 25.f * frand()) + vec3(0, 2.f, 0), 9.f + 4.f * frand(), 22.f + 10.f * frand(), 9.f,
          water ? vec3(0.85f, 0.88f, 0.92f) : vec3(0.32f, 0.28f, 0.25f), water ? 0.5f : 0.6f, SPR_SMOKE, 1.4f, 0.05f);
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
  // move toward the target and stop on it (at exactly 1 the old ramp took the fading branch every other frame, so the
  // cloak flickered between ~98% and fully engaged)
  W.stealth = tgt > W.stealth ? std::min(tgt, W.stealth + 0.8f * dt) : std::max(tgt, W.stealth - 1.1f * dt);
  W.front = W.stealth >= 1.f ? 1e3f : -9.5f + 19.f * W.stealth;   // fully spread: the whole craft, wavefront gone
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
  // laser bolts fly, then new ones leave the lenses (after the physics step, from where the turrets are now)
  updateBolts(dt);
  W.laserCD -= dt;
  if (wr && W.wantFire && W.lasers > 0.97f && W.laserCD <= 0) { fireLaser(); W.laserCD = 0.12f; }
  W.wantFire = false;
  // bombs fall (a little drag), trail violet sparks, and go off on the ground, the sea or near an aircraft
  for (size_t i = 0; i < W.bombs.size(); i++) {
    WraithState::Bomb& b = W.bombs[i];
    b.t += dt;
    b.v += vec3(0, -G0, 0) * dt - b.v * (2e-5f * length(b.v) * dt);
    b.p += b.v * dt;
    if (frand() < 0.6f) spawn(b.p, b.v * 0.9f + rndDir() * 3.f, 0.4f, 0.25f, 0.5f, vec3(0.6f, 0.25f, 1.f) * 3.f, 1.f, SPR_SPARK, 2.f, 0.f);
    float g = groundAt(b.p);
    bool near = false; float tt;
    if (traffic.rayHit(b.p, vec3(0, -1, 0), 1.f, tt) >= 0) near = true;
    for (auto& c : traffic.craft) if (c.alive && c.role != TrafficCraft::ESCORT && length(c.pos - b.p) < 25.f) near = true;
    if (b.p.y <= g || near || b.t > 60.f) {
      vec3 at = b.p; if (at.y < g) at.y = g;
      detonate(at, g_world.height(at.x, at.z) < 0.3f && at.y < 1.f);
      if (i == 0 && W.cam.on && W.cam.phase == 0) {   // the bomb the camera was chasing went off: pull out to watch it
        W.cam.phase = 1; W.cam.t = 0; W.cam.blastP = at;
        vec3 h = W.cam.pos - at; h.y = 0;
        W.cam.orbit = atan2f(h.x, h.z + 1e-4f) + 0.5f;   // swing round to a three-quarter view
      }
      W.bombs.erase(W.bombs.begin() + i); i--;
    }
  }
  updateBombCam(dt);
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
    if (const char* e = getenv("WRSURF")) pv.wr[5][1] = (float)atof(e);   // (render checks: hold a ruddervator deflection)
    pv.wr[6][0] = W.bombLoaded; pv.wr[6][1] = W.front; pv.wr[6][2] = W.armed ? 1.f : 0.f;
  }
  FxVisual& fx = fp.fx;
  fx.beams = 0;
  for (int i = (int)W.bolts.size() - 1; i >= 0 && fx.beams < 16; i--) {
    const WraithState::Bolt& b = W.bolts[i];
    if (b.len <= 0.05f) continue;
    vec3 tail = b.h - b.d * b.len;   // the streak trails along the aim line (it leaves the lens in the craft's frame)
    float k = clampf((b.life - b.age) / 0.25f, 0.f, 1.f);
    float* A = fx.beamA[fx.beams]; float* B = fx.beamB[fx.beams];
    A[0] = tail.x; A[1] = tail.y; A[2] = tail.z; A[3] = 0.22f;
    B[0] = b.h.x; B[1] = b.h.y; B[2] = b.h.z; B[3] = k;
    fx.beams++;
  }
  fx.bombs = 0;
  for (size_t i = 0; i < W.bombs.size() && fx.bombs < 8; i++) { float* o = fx.bomb[fx.bombs++]; o[0] = W.bombs[i].p.x; o[1] = W.bombs[i].p.y; o[2] = W.bombs[i].p.z; o[3] = 0.29f; }
  fx.blasts = 0;
  for (size_t i = 0; i < W.blasts.size() && fx.blasts < 6; i++) {
    const WraithState::Blast& b = W.blasts[i];
    float* o = fx.blast[fx.blasts]; float* I = fx.blastI[fx.blasts]; fx.blasts++;
    o[0] = b.p.x; o[1] = b.p.y; o[2] = b.p.z; o[3] = b.R;   // the shader grows the fireball and lifts the cap
    I[0] = b.age; I[1] = 1.f; I[2] = 0; I[3] = 0;
  }
  // bomb impact prediction for the cockpit's floor and chin displays: a bomb released now, falling with the same
  // physics as a real one (gravity, a little drag), until it meets the ground or the sea
  fx.pip[3] = 0.f;
  if (plane.spec && plane.spec->special == 2 && !plane.onGround && !crashed && camMode == 1) {
    vec3 p = plane.pos + plane.q.rotate(kBayBomb - vec3(0, 0.25f, 0)), v = plane.vel + plane.up() * -3.f;
    const float dt = 0.05f;
    for (int i = 0; i < 1200; i++) {
      v += vec3(0, -G0, 0) * dt - v * (2e-5f * length(v) * dt);
      p += v * dt;
      if (p.y < 2600.f) {
        float g = groundAt(p);
        if (p.y <= g) { fx.pip[0] = p.x; fx.pip[1] = g; fx.pip[2] = p.z; fx.pip[3] = 1.f; break; }
      }
    }
  }
  // bomb camera: its picture goes to the footwell floor screens while it exists (buildFeedCameras)
  fx.feed[3] = 0.f;
  if (plane.spec && plane.spec->special == 2 && camMode == 1 && !crashed && W.cam.on) {
    fx.feed[0] = W.cam.look.x; fx.feed[1] = W.cam.look.y; fx.feed[2] = W.cam.look.z; fx.feed[3] = 1.f;
  }
  // glassed craters join the crash crater (if any)
  for (size_t i = 0; i < W.craters.size() && fp.wreck.craterN < 24; i++) {
    float* c = fp.wreck.crater[fp.wreck.craterN++];
    c[0] = W.craters[i].x; c[1] = W.craters[i].z; c[2] = W.craters[i].R; c[3] = W.craters[i].D;
  }
  // laser scorch pits (newest first, in case the list is full)
  for (int i = (int)W.scorch.size() - 1; i >= 0 && fp.wreck.craterN < 24; i--) {
    float* c = fp.wreck.crater[fp.wreck.craterN++];
    c[0] = W.scorch[i].x; c[1] = W.scorch[i].z; c[2] = W.scorch[i].R; c[3] = W.scorch[i].D;
  }
  // a young detonation lights up its surroundings (borrowing the exhaust light)
  for (const auto& b : W.blasts)
    if (b.age < 0.4f) {
      float k = 1.f - b.age / 0.4f;
      fp.flameLightPos = b.p + vec3(0, b.R * (0.3f + 1.2f * b.age), 0);
      fp.flameLight = lerp(vec3(0.5f, 0.2f, 1.f), vec3(1.f, 0.85f, 1.f), k * k) * (45000.f * k * k);
      break;
    }
}
