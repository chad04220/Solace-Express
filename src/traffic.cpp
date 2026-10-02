// Air Xpress - AI traffic (see traffic.h). Kinematic aircraft: airport traffic follows taxi lines and flies real
// circuits with glideslope approaches; cruisers cross the map; XR-9 formations make high-speed passes; a display team
// flies an aerobatic sequence with smoke.
#include "traffic.h"
#include "aircraft.h"
#include "world.h"
#include "models.h"
#include "renderer.h"
#include <algorithm>

static const float G = 9.81f;
static float wrapPi(float a) { while (a > PI) a -= 2 * PI; while (a < -PI) a += 2 * PI; return a; }
static float gearH(const AircraftSpec& s) { return s.taildragger ? s.fusRad + 0.45f : s.fusRad * 1.3f + (s.engineType == ENG_JET || s.engines == 2 ? 0.75f : 0.55f); }
static bool bigAircraft(int spec) { return spec == 5 || spec == 6; }
static bool jetLike(int spec) { return kAircraft[spec].engineType == ENG_JET; }

// Runway-local frame used by the shader's airport surfaces: u along the runway heading, v across it
static vec3 apWorld(const Airport& a, float u, float v, float y) {
  float h = a.heading * DEG, s = sinf(h), c = cosf(h);
  return vec3(a.x + u * s + v * c, y, a.z - u * c + v * s);
}
static vec2 apLocal(const Airport& a, vec3 p) {
  float h = a.heading * DEG, s = sinf(h), c = cosf(h), dx = p.x - a.x, dz = p.z - a.z;
  return vec2(dx * s - dz * c, dx * c + dz * s);
}
static float apSide(int ai) { return (ai & 1) ? 1.f : -1.f; }   // apron side, as in World::build / runwayMaterial
static bool apUsable(const Airport& a) { return a.surface == 0 && a.size > 0; }
static float apOff(const Airport& a) { return a.width * 0.5f + (a.size == 2 ? 170.f : 85.f); }
static float apLane(const Airport& a) { return a.width * 0.5f + 45.f; }
static float standU(const Airport& a, int k) { return -a.length * 0.05f + 22.5f + 45.f * k; }
static int standRange(const Airport& a) { return std::max(1, (int)((a.length * 0.33f - 30.f) / 45.f)); }

float Traffic::rnd() { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return (seed & 0xFFFFFF) / 16777216.f; }

void Traffic::reset() { craft.clear(); puffs.clear(); booms.clear(); spawnT = 0; formationT = rnd(35.f, 70.f); stuntT = 0; }

int Traffic::count(int role) const { int n = 0; for (auto& c : craft) if (c.alive && c.role == role) n++; return n; }

void Traffic::attitudeToQuat(TrafficCraft& c) const {
  c.q = quat::axisAngle(vec3(0, 1, 0), -c.hdg) * quat::axisAngle(vec3(1, 0, 0), c.pitch) * quat::axisAngle(vec3(0, 0, 1), -c.bank);
}

// rotation whose columns are the given orthonormal axes
static quat quatFromBasis(vec3 x, vec3 y, vec3 z) {
  float tr = x.x + y.y + z.z; quat q;
  if (tr > 0) { float s = sqrtf(tr + 1.f) * 2; q = quat(0.25f * s, (y.z - z.y) / s, (z.x - x.z) / s, (x.y - y.x) / s); }
  else if (x.x > y.y && x.x > z.z) { float s = sqrtf(1.f + x.x - y.y - z.z) * 2; q = quat((y.z - z.y) / s, 0.25f * s, (y.x + x.y) / s, (z.x + x.z) / s); }
  else if (y.y > z.z) { float s = sqrtf(1.f + y.y - x.x - z.z) * 2; q = quat((z.x - x.z) / s, (y.x + x.y) / s, 0.25f * s, (z.y + y.z) / s); }
  else { float s = sqrtf(1.f + z.z - x.x - y.y) * 2; q = quat((x.y - y.x) / s, (z.x + x.z) / s, (z.y + y.z) / s, 0.25f * s); }
  q.normalize(); return q;
}

static void liveries(TrafficCraft& c, float r) {
  const AircraftSpec& s = kAircraft[c.spec];
  vec3 stripes[6] = {s.colStripe, vec3(0.75f, 0.1f, 0.1f), vec3(0.1f, 0.3f, 0.75f), vec3(0.1f, 0.55f, 0.25f), vec3(0.9f, 0.55f, 0.1f), vec3(0.15f, 0.15f, 0.18f)};
  c.colBase = s.colBase; c.colStripe = stripes[(int)(r * 5.99f)];
  if (c.spec == kResearchJet) c.colStripe = s.colStripe;
}

// ------------------------------------------------------------------ spawning
void Traffic::spawnAirport(int ai, vec3 player) {
  (void)player;
  const Airport& a = g_world.airports[ai];
  int nst = standRange(a);
  // pick a free stand
  for (int tries = 0; tries < 8; tries++) {
    int k = (int)rnd(-nst, nst + 0.99f);
    bool used = false;
    for (auto& c : craft) if (c.alive && c.role == TrafficCraft::AIRPORT && c.airport == ai && c.stand == k) used = true;
    if (used) continue;
    TrafficCraft c; c.id = nextId++;
    int small[5] = {0, 1, 2, 3, 4};
    c.spec = a.size == 2 && rnd() < 0.5f ? (rnd() < 0.5f ? 5 : 6) : small[(int)(rnd() * 4.99f)];
    if (a.length < kAircraft[c.spec].runwayNeeded(a.elev) * 1.1f) c.spec = 0;
    c.role = TrafficCraft::AIRPORT; c.airport = ai; c.stand = k; c.state = TrafficCraft::PARKED; c.timer = rnd(8.f, 70.f);
    float side = apSide(ai);
    c.pos = apWorld(a, standU(a, k), side * (apOff(a) - 12.f - kAircraft[c.spec].fusLen * 0.3f), 0);
    vec3 face = apWorld(a, 0, -side, 0) - apWorld(a, 0, 0, 0);   // nose towards the runway
    c.hdg = atan2f(face.x, -face.z);
    c.pos.y = g_world.height(c.pos.x, c.pos.z) + gearH(kAircraft[c.spec]);
    c.pitch = kAircraft[c.spec].taildragger ? 11.3f * DEG : 0.f;
    c.gear = 1; c.throttle = 0; liveries(c, rnd());
    if (rnd() < 0.45f) {   // an arrival: joins the downwind leg and lands, then taxis to this stand
      const AircraftSpec& sp = kAircraft[c.spec];
      float kk = bigAircraft(c.spec) || jetLike(c.spec) ? 1.6f : 1.f, pat = a.elev + (kk > 1.f ? 450.f : 300.f);
      float uTD = -a.length * 0.5f + std::max(150.f, a.length * 0.12f), sd = -side;
      c.state = TrafficCraft::CIRCUIT;
      c.path = {apWorld(a, a.length * 0.5f + 1500.f * kk, sd * 1100.f * kk, pat), apWorld(a, 0, sd * 1100.f * kk, pat),
                apWorld(a, -a.length * 0.5f - 900.f * kk, sd * 1100.f * kk, pat), apWorld(a, -a.length * 0.5f - 2200.f * kk, sd * 650.f * kk, pat - 90.f * kk),
                apWorld(a, -a.length * 0.5f - 2700.f * kk, 0, a.elev + gearH(sp) + (uTD + a.length * 0.5f + 2700.f * kk) * 0.0524f)};
      c.wp = (int)rnd(0.f, 2.99f);
      vec3 from = c.wp > 0 ? c.path[c.wp - 1] : apWorld(a, a.length * 0.5f + 600.f, sd * 400.f, pat);
      c.pos = from; vec3 dd = c.path[c.wp] - from; c.hdg = atan2f(dd.x, -dd.z); c.pitch = 0; c.bank = 0;
      c.speed = sp.vref * 1.25f; c.gear = sp.retract ? 0.f : 1.f; c.throttle = 0.6f;
    }
    attitudeToQuat(c);
    craft.push_back(c);
    return;
  }
}

void Traffic::spawnCruiser(vec3 player) {
  TrafficCraft c; c.id = nextId++;
  c.spec = (int)(rnd() * 6.99f);
  c.role = TrafficCraft::CRUISER; c.state = TrafficCraft::FLY;
  float a = rnd(0, 2 * PI), d = rnd(9000.f, 13000.f);
  c.pos = player + vec3(cosf(a) * d, 0, sinf(a) * d);
  float g = std::max(g_world.height(c.pos.x, c.pos.z), g_world.height(player.x, player.z));
  c.pos.y = std::max(g + 600.f, clampf(player.y + rnd(-400.f, 900.f), 700.f, 3500.f));
  vec3 aim = player + vec3(rnd(-3000.f, 3000.f), 0, rnd(-3000.f, 3000.f)) - c.pos;
  c.hdg = atan2f(aim.x, -aim.z);
  c.speed = kAircraft[c.spec].cruise; c.gear = kAircraft[c.spec].retract ? 0.f : 1.f; c.throttle = 0.75f;
  c.path = {c.pos + vec3(sinf(c.hdg), 0, -cosf(c.hdg)) * 40000.f}; c.wp = 0;
  liveries(c, rnd());
  attitudeToQuat(c);
  craft.push_back(c);
}

void Traffic::spawnFormation(vec3 player, vec3 playerVel) {
  int n = rnd() < 0.5f ? 2 : 3;
  float a = rnd(0, 2 * PI);
  if (length(playerVel) > 20.f && rnd() < 0.6f) { vec3 f = normalize(playerVel); a = atan2f(f.z, f.x) + rnd(-0.8f, 0.8f); }   // usually from ahead
  vec3 start = player + vec3(cosf(a), 0, sinf(a)) * 9000.f;
  vec3 lateral = normalize(cross(player - start, vec3(0, 1, 0)) + vec3(1e-3f, 0, 0));
  vec3 aim = player + lateral * rnd(-260.f, 260.f);
  float ground = std::max(std::max(g_world.height(start.x, start.z), g_world.height(player.x, player.z)), 0.f);
  start.y = aim.y = std::max(player.y + rnd(-40.f, 90.f), ground + 150.f);
  vec3 dir = normalize(aim - start);
  int leadIdx = (int)craft.size();
  vec3 offs[3] = {vec3(0, 0, 0), vec3(-24, 0, 28), vec3(24, 0, 28)};
  float spd = rnd(330.f, 470.f);
  for (int i = 0; i < n; i++) {
    TrafficCraft c; c.id = nextId++;
    c.spec = kResearchJet; c.role = TrafficCraft::FORMATION; c.state = TrafficCraft::FLY;
    c.hdg = atan2f(dir.x, -dir.z); c.pitch = 0; c.bank = 0;
    attitudeToQuat(c);
    c.leader = i == 0 ? -1 : leadIdx; c.offset = offs[i];
    c.pos = start + c.q.rotate(c.offset);
    c.speed = spd; c.vel = dir * spd; c.gear = 0; c.flaps = 0; c.throttle = 1; c.ab = 1;
    c.timer = 0; c.man = 0;
    liveries(c, 0);
    craft.push_back(c);
  }
}

void Traffic::spawnDisplayTeam(int ai) {
  const Airport& a = g_world.airports[ai];
  float side = apSide(ai);
  int leadIdx = (int)craft.size();
  for (int i = 0; i < 2; i++) {
    TrafficCraft c; c.id = nextId++;
    c.spec = 0;   // Kestrel T2 aerobatic trainers
    c.role = TrafficCraft::STUNT; c.state = TrafficCraft::FLY; c.airport = ai;
    c.leader = i == 0 ? -1 : leadIdx; c.offset = vec3(13.f, -1.5f, 11.f);
    c.hdg = (a.heading + 90.f * side) * DEG;
    c.pitch = 0; c.bank = 0; attitudeToQuat(c);
    c.pos = apWorld(a, -1500.f, -side * 1800.f, a.elev + 550.f) + (i ? c.q.rotate(c.offset) : vec3());
    c.speed = 80.f; c.gear = 0; c.throttle = 0.9f;
    c.colBase = vec3(0.92f, 0.92f, 0.94f); c.colStripe = i == 0 ? vec3(0.8f, 0.08f, 0.08f) : vec3(0.08f, 0.2f, 0.75f);
    c.smokeCol = i == 0 ? vec3(1.f, 1.f, 1.f) : vec3(0.95f, 0.2f, 0.18f);
    craft.push_back(c);
  }
}

// ------------------------------------------------------------------ motion
// Coordinated kinematic flight towards a point: bank-to-turn, vertical speed towards the target height
void Traffic::updateFlight(TrafficCraft& c, float dt, vec3 target, float tgtSpeed, float maxBank) {
  vec3 d = target - c.pos;
  float desired = atan2f(d.x, -d.z), err = wrapPi(desired - c.hdg);
  float bankT = clampf(err * 1.6f, -maxBank, maxBank);
  c.bank += clampf(bankT - c.bank, -0.6f * dt, 0.6f * dt);
  c.hdg = wrapPi(c.hdg + G * tanf(c.bank) / std::max(c.speed, 20.f) * dt);
  bool jet = jetLike(c.spec);
  float vsT = clampf(d.y * 0.12f, jet ? -14.f : -6.f, jet ? 14.f : 7.f);
  c.vs += clampf(vsT - c.vs, -2.f * dt, 2.f * dt);
  c.speed += clampf(tgtSpeed - c.speed, -2.f * dt, 2.f * dt);
  float gam = asinf(clampf(c.vs / std::max(c.speed, 1.f), -0.6f, 0.6f));
  c.pitch += clampf(gam + 2.5f * DEG - c.pitch, -0.3f * dt, 0.3f * dt);
  vec3 f(sinf(c.hdg) * cosf(gam), sinf(gam), -cosf(c.hdg) * cosf(gam));
  c.vel = f * c.speed; c.pos += c.vel * dt;
  c.ctlRoll = clampf((bankT - c.bank) * 3.f, -1, 1); c.ctlPitch = clampf((vsT - c.vs) * 0.2f, -0.5f, 0.5f);
  attitudeToQuat(c);
}

void Traffic::groundTaxi(TrafficCraft& c, float dt, float maxSpeed, vec3 playerPos, bool playerOnGround) {
  const AircraftSpec& s = kAircraft[c.spec];
  float spT = 0;
  if (c.wp < (int)c.path.size()) {
    vec3 d = c.path[c.wp] - c.pos; d.y = 0;
    float dist = length(d);
    float err = wrapPi(atan2f(d.x, -d.z) - c.hdg);
    spT = maxSpeed * (fabsf(err) > 0.5f ? 0.35f : 1.f) * clampf(dist / 15.f, 0.4f, 1.f);
    c.hdg = wrapPi(c.hdg + clampf(err * 1.2f, -0.5f, 0.5f) * clampf(c.speed / 4.f, 0.3f, 1.f) * dt);
    c.ctlYaw = clampf(err * 2.f, -1, 1);
    if (dist < 5.f) c.wp++;
  }
  // hold for the player or another aircraft taxiing ahead
  vec3 f(sinf(c.hdg), 0, -cosf(c.hdg)), tp = playerPos - c.pos; tp.y = 0;
  if (playerOnGround && length(tp) < 50.f && dot(normalize(tp), f) > 0.6f) spT = 0;
  for (auto& o : craft) {
    if (o.id == c.id || !o.alive || o.role != TrafficCraft::AIRPORT || o.airport != c.airport) continue;
    vec3 op = o.pos - c.pos; op.y = 0; float od = length(op);
    if (od < 45.f && od > 0.1f && dot(op / od, f) > 0.75f && (o.state != TrafficCraft::PARKED || od < 25.f)) spT = 0;
  }
  c.speed += clampf(spT - c.speed, -2.5f * dt, 1.2f * dt);
  c.pos += f * c.speed * dt; c.vel = f * c.speed;
  c.pos.y = g_world.height(c.pos.x, c.pos.z) + gearH(s);
  c.pitch = s.taildragger ? 11.3f * DEG : 0.f; c.bank = 0;
  attitudeToQuat(c);
}

bool Traffic::runwayBusy(int ai, int self, vec3 playerPos, bool playerOnGround) const {
  const Airport& a = g_world.airports[ai];
  for (auto& o : craft) {
    if (!o.alive || o.id == self || o.role != TrafficCraft::AIRPORT || o.airport != ai) continue;
    if (o.state == TrafficCraft::LINEUP || o.state == TrafficCraft::TAKEOFF || o.state == TrafficCraft::ROLLOUT) return true;
    if (o.state == TrafficCraft::FINAL && length(o.pos - apWorld(a, -a.length * 0.5f, 0, o.pos.y)) < 3500.f) return true;
  }
  vec2 pl = apLocal(a, playerPos);
  if (fabsf(pl.x) < a.length * 0.5f + 150.f && fabsf(pl.y) < a.width * 0.5f + 25.f && playerPos.y - a.elev < 120.f) return true;
  (void)playerOnGround;
  return false;
}

void Traffic::updateAirport(TrafficCraft& c, float dt, vec3 playerPos, bool playerOnGround) {
  const Airport& a = g_world.airports[c.airport];
  const AircraftSpec& s = kAircraft[c.spec];
  float side = apSide(c.airport), lane = apLane(a), hold = a.width * 0.5f + 26.f;
  float uC = -a.length * 0.38f, uE = a.length * 0.28f, uTD = -a.length * 0.5f + std::max(150.f, a.length * 0.12f);
  float rwyHdg = a.heading * DEG, gh = gearH(s);
  float k = bigAircraft(c.spec) || jetLike(c.spec) ? 1.6f : 1.f;
  float patAlt = a.elev + (k > 1.f ? 450.f : 300.f);
  float vapp = s.vref, vclimb = std::max(s.vr * 1.3f, s.vref * 1.15f);
  c.timer -= dt;
  switch (c.state) {
    case TrafficCraft::PARKED: {
      c.speed = 0; c.throttle = approach(c.throttle, 0.05f, 1.f, dt);
      if (c.timer <= 0) {
        c.state = TrafficCraft::TAXI_OUT; c.throttle = 0.3f;
        float us = standU(a, c.stand);
        c.path = {apWorld(a, us, side * lane, 0), apWorld(a, uC, side * lane, 0), apWorld(a, uC, side * hold, 0)}; c.wp = 0;
      }
      break;
    }
    case TrafficCraft::TAXI_OUT:
      groundTaxi(c, dt, 11.f, playerPos, playerOnGround); c.flaps = approach(c.flaps, s.engineType == ENG_JET ? 0.3f : 0.15f, 0.5f, dt);
      if (c.wp >= (int)c.path.size()) { c.state = TrafficCraft::HOLD; c.timer = rnd(4.f, 12.f); }
      break;
    case TrafficCraft::HOLD:
      c.speed = approach(c.speed, 0, 3.f, dt);
      if (c.timer <= 0 && !runwayBusy(c.airport, c.id, playerPos, playerOnGround)) {
        c.state = TrafficCraft::LINEUP; c.path = {apWorld(a, uC + 25.f, 0, 0), apWorld(a, uC + 90.f, 0, 0)}; c.wp = 0;
      }
      break;
    case TrafficCraft::LINEUP:
      groundTaxi(c, dt, 6.f, playerPos, playerOnGround);
      if (c.wp >= 1 && fabsf(wrapPi(c.hdg - rwyHdg)) < 0.08f) { c.state = TrafficCraft::TAKEOFF; c.hdg = rwyHdg; }
      if (c.wp >= (int)c.path.size()) { c.state = TrafficCraft::TAKEOFF; }
      break;
    case TrafficCraft::TAKEOFF: {
      c.throttle = 1;
      vec2 l = apLocal(a, c.pos);
      c.hdg = wrapPi(rwyHdg - clampf(l.y * 0.02f, -0.1f, 0.1f));
      c.speed += (jetLike(c.spec) ? 3.2f : 2.4f) * dt;
      vec3 f(sinf(c.hdg), 0, -cosf(c.hdg));
      c.pos += f * c.speed * dt; c.pos.y = g_world.height(c.pos.x, c.pos.z) + gh;
      c.pitch = s.taildragger ? 11.3f * DEG * clampf(1.f - c.speed / (s.vr * 0.6f), 0, 1) : 0.f; c.bank = 0; c.vel = f * c.speed;
      attitudeToQuat(c);
      if (c.speed >= s.vr) { c.state = TrafficCraft::CLIMB; c.vs = 0; }
      break;
    }
    case TrafficCraft::CLIMB: {
      vec3 up = apWorld(a, a.length * 0.5f + 900.f * k, 0, patAlt);
      updateFlight(c, dt, up, vclimb, 15.f * DEG);
      float agl = c.pos.y - g_world.height(c.pos.x, c.pos.z) - gh;
      if (agl > 25.f && s.retract) c.gear = approach(c.gear, 0, 0.8f, dt);
      if (agl > 120.f) c.flaps = approach(c.flaps, 0, 0.5f, dt);
      if (length(vec2(c.pos.x - up.x, c.pos.z - up.z)) < 400.f || c.pos.y > patAlt - 30.f) {
        if (rnd() < 0.25f) {   // some depart the area instead of flying the circuit; they become cruisers
          c.role = TrafficCraft::CRUISER; c.state = TrafficCraft::FLY; c.gear = s.retract ? 0.f : 1.f;
          float hd = c.hdg + rnd(-1.2f, 1.2f);
          c.path = {c.pos + vec3(sinf(hd), 0, -cosf(hd)) * 40000.f + vec3(0, rnd(400.f, 1500.f), 0)}; c.wp = 0;
          break;
        }
        c.state = TrafficCraft::CIRCUIT;
        float sp = -side;
        c.path = {apWorld(a, a.length * 0.5f + 1500.f * k, sp * 1100.f * k, patAlt), apWorld(a, 0, sp * 1100.f * k, patAlt),
                  apWorld(a, -a.length * 0.5f - 900.f * k, sp * 1100.f * k, patAlt), apWorld(a, -a.length * 0.5f - 2200.f * k, sp * 650.f * k, patAlt - 90.f * k),
                  apWorld(a, -a.length * 0.5f - 2700.f * k, 0, a.elev + gh + (uTD + a.length * 0.5f + 2700.f * k) * 0.0524f)};
        c.wp = 0;
      }
      break;
    }
    case TrafficCraft::CIRCUIT: {
      if (c.wp >= (int)c.path.size()) { c.state = TrafficCraft::FINAL; break; }
      vec3 tgt = c.path[c.wp];
      updateFlight(c, dt, tgt, c.wp >= 2 ? vapp * 1.25f : vclimb * 1.1f, 25.f * DEG);
      if (c.wp >= 2) c.flaps = approach(c.flaps, 0.5f, 0.4f, dt);
      if (c.wp >= 3 && s.retract) c.gear = approach(c.gear, 1, 0.8f, dt);
      c.throttle = c.wp >= 3 ? 0.45f : 0.65f;
      if (length(vec2(c.pos.x - tgt.x, c.pos.z - tgt.z)) < 350.f * k) {
        c.wp++;
        if (c.wp == 3) {   // sequencing: someone else on final - extend downwind (fly back up the leg and try again)
          for (auto& o : craft)
            if (o.id != c.id && o.alive && o.role == TrafficCraft::AIRPORT && o.airport == c.airport &&
                (o.state == TrafficCraft::FINAL || (o.state == TrafficCraft::CIRCUIT && o.wp >= 3))) { c.wp = 1; break; }
        }
      }
      break;
    }
    case TrafficCraft::FINAL: {
      vec2 l = apLocal(a, c.pos);
      float du = uTD - l.x;                     // distance to the touchdown point along the runway
      float hRw = a.elev + gh;
      // go around if the runway is blocked (the player sitting on it)
      if (du < 900.f && du > 200.f && runwayBusy(c.airport, c.id, playerPos, playerOnGround)) {
        c.state = TrafficCraft::CLIMB; c.gear = 1; break;
      }
      float hdgT = rwyHdg - clampf(l.y * 0.004f, -0.35f, 0.35f);
      float err = wrapPi(hdgT - c.hdg);
      c.bank += clampf(clampf(err * 2.f, -0.35f, 0.35f) - c.bank, -0.5f * dt, 0.5f * dt);
      c.hdg = wrapPi(c.hdg + G * tanf(c.bank) / std::max(c.speed, 20.f) * dt);
      float altT = hRw + std::max(du, 0.f) * 0.0524f;
      float vsT = clampf(-c.speed * 0.0524f + (altT - c.pos.y) * 0.3f, -8.f, 4.f);
      float hab = c.pos.y - g_world.height(c.pos.x, c.pos.z) - gh;
      if (hab < 7.f) vsT = std::max(-1.2f, -0.15f * hab - 0.4f);   // flare
      c.vs += clampf(vsT - c.vs, -3.f * dt, 3.f * dt);
      c.speed += clampf(vapp - c.speed, -1.5f * dt, 1.5f * dt);
      c.flaps = approach(c.flaps, 1, 0.4f, dt); if (s.retract) c.gear = approach(c.gear, 1, 1.f, dt);
      c.throttle = 0.3f;
      float gam = asinf(clampf(c.vs / c.speed, -0.5f, 0.5f));
      c.pitch += clampf(gam + (hab < 7.f ? 5.f : 2.f) * DEG - c.pitch, -0.2f * dt, 0.2f * dt);
      vec3 f(sinf(c.hdg) * cosf(gam), sinf(gam), -cosf(c.hdg) * cosf(gam));
      c.vel = f * c.speed; c.pos += c.vel * dt;
      attitudeToQuat(c);
      if (hab <= 0.05f && du < 300.f) {
        c.state = TrafficCraft::ROLLOUT; c.pos.y = g_world.height(c.pos.x, c.pos.z) + gh; c.vs = 0;
        puffs.push_back({c.pos - vec3(0, gh, 0), vec3(0, 0.5f, 0), vec3(0.75f), 2.f, 1.2f, 1.6f, 0.45f, SPR_SMOKE});   // tyre smoke
      }
      break;
    }
    case TrafficCraft::ROLLOUT: {
      vec2 l = apLocal(a, c.pos);
      float rem = std::max(uE - l.x - 20.f, 30.f);
      float dec = clampf((c.speed * c.speed - 100.f) / (2.f * rem), 1.f, 4.f);
      c.speed = std::max(c.speed - dec * dt, 9.f);
      c.hdg = wrapPi(rwyHdg - clampf(l.y * 0.02f, -0.1f, 0.1f));
      vec3 f(sinf(c.hdg), 0, -cosf(c.hdg));
      c.pos += f * c.speed * dt; c.pos.y = g_world.height(c.pos.x, c.pos.z) + gh; c.vel = f * c.speed;
      c.pitch = approach(c.pitch, s.taildragger ? 11.3f * DEG : 0.f, 2.f, dt); c.bank = 0; c.throttle = 0.1f;
      c.flaps = approach(c.flaps, 0, 0.3f, dt);
      attitudeToQuat(c);
      if (l.x >= uE - 15.f) {
        c.state = TrafficCraft::TAXI_IN;
        float us = standU(a, c.stand);
        c.path = {apWorld(a, uE, side * hold, 0), apWorld(a, uE, side * lane, 0), apWorld(a, us, side * lane, 0),
                  apWorld(a, us, side * (apOff(a) - 12.f - s.fusLen * 0.3f), 0)};
        c.wp = 0;
      }
      break;
    }
    case TrafficCraft::TAXI_IN:
      groundTaxi(c, dt, 11.f, playerPos, playerOnGround); c.throttle = 0.25f;
      if (c.wp >= (int)c.path.size()) {   // at the stand: a tug turns it round to face the runway again
        c.speed = approach(c.speed, 0, 2.f, dt);
        vec3 face = apWorld(a, 0, -side, 0) - apWorld(a, 0, 0, 0);
        float want = atan2f(face.x, -face.z);
        c.hdg = wrapPi(c.hdg + clampf(wrapPi(want - c.hdg), -0.25f * dt, 0.25f * dt));
        attitudeToQuat(c);
        if (fabsf(wrapPi(want - c.hdg)) < 0.02f) { c.state = TrafficCraft::PARKED; c.timer = rnd(25.f, 120.f); }
      }
      break;
    default: break;
  }
}

// Aerobatic sequencer: each manoeuvre is a list of (duration, pitch rate, roll rate, yaw rate) body-rate segments
struct ManSeg { float dur, p, r, y; };
static const int kManCount = 8;
static int manSegs(int m, ManSeg* o) {
  const float pr = 0.42f;   // loop pitch rate (rad/s): ~190 m radius at 80 m/s
  switch (m) {
    case 0: o[0] = {2 * PI / pr, pr, 0, 0}; return 1;                                                  // loop
    case 1: o[0] = {3.6f, 0, 2 * PI / 3.6f, 0}; return 1;                                               // aileron roll
    case 2: o[0] = {9.f, 0.33f, 2 * PI / 9.f, 0}; return 1;                                             // barrel roll
    case 3: o[0] = {PI / pr, pr, 0, 0}; o[1] = {2.2f, 0, PI / 2.2f, 0}; return 2;                       // Immelmann
    case 4: o[0] = {2.2f, 0, PI / 2.2f, 0}; o[1] = {PI / pr, pr, 0, 0}; return 2;                       // split-S
    case 5: o[0] = {1.25f * PI / pr, pr, 0, 0}; o[1] = {2.f, 0, PI / 2.f, 0};                           // Cuban eight
            o[2] = {1.5f * PI / pr, pr, 0, 0}; o[3] = {2.f, 0, PI / 2.f, 0}; o[4] = {0.25f * PI / pr, pr, 0, 0}; return 5;
    case 6: for (int i = 0; i < 4; i++) { o[i * 2] = {0.7f, 0, 0.5f * PI / 0.7f, 0}; o[i * 2 + 1] = {0.55f, 0, 0, 0}; } return 8;   // 4-point roll
    case 7: o[0] = {3.7f, 0.5f * PI / 3.7f, 0, 0}; o[1] = {2.6f, 0, 0, 0}; o[2] = {1.8f, 0, 0, PI / 1.8f};  // hammerhead
            o[3] = {2.4f, 0, 0, 0}; o[4] = {3.7f, 0.5f * PI / 3.7f, 0, 0}; return 5;
  }
  return 0;
}

void Traffic::updateStunt(TrafficCraft& c, float dt) {
  const Airport& a = g_world.airports[c.airport];
  float side = apSide(c.airport);
  vec3 box = apWorld(a, 0, -side * 900.f, a.elev + 520.f);
  float ground = g_world.height(c.pos.x, c.pos.z), agl = c.pos.y - ground;
  vec3 f = c.q.rotate(vec3(0, 0, -1));
  if (c.man >= 0) {
    ManSeg seg[10]; int ns = manSegs(c.man, seg);
    const ManSeg& s = seg[c.seg];
    // body rates: pitch about +x (nose up), roll about -z (right roll), yaw about -y (nose right)
    c.q = c.q * quat::axisAngle(vec3(1, 0, 0), s.p * dt) * quat::axisAngle(vec3(0, 0, -1), s.r * dt) * quat::axisAngle(vec3(0, -1, 0), s.y * dt);
    c.q.normalize();
    c.ctlPitch = s.p > 0 ? 0.8f : 0.f; c.ctlRoll = s.r > 0 ? 1.f : 0.f; c.ctlYaw = s.y > 0 ? 1.f : 0.f;
    c.segT += dt;
    if (c.segT >= s.dur) { c.segT = 0; if (++c.seg >= ns) { c.man = -1; c.smoke = false; c.timer = rnd(2.f, 5.f); } }
    if (agl < 140.f && f.y < -0.2f) { c.man = -1; c.smoke = false; }   // safety: abort low and nose-down
    c.speed += (-G * f.y * 0.85f + (82.f - c.speed) * 0.35f) * dt;
    c.speed = clampf(c.speed, 32.f, 140.f);
    c.vel = f * c.speed; c.pos += c.vel * dt;
    // keep kinematic attitude in sync for the repositioning controller
    vec3 r = c.q.rotate(vec3(1, 0, 0));
    c.hdg = atan2f(f.x, -f.z); c.pitch = asinf(clampf(f.y, -1, 1)); c.bank = atan2f(-r.y, normalize(cross(r, f)).y);
    c.vs = c.vel.y;
    return;
  }
  // reposition: a racetrack along the crowd line (parallel to the runway); a manoeuvre starts on each run-in
  vec3 U = apWorld(a, 1, 0, 0) - apWorld(a, 0, 0, 0);
  vec3 endA = box + U * 1600.f, endB = box - U * 1600.f;
  if (c.wp != 0 && c.wp != 1) c.wp = 0;
  vec3 tgt = c.wp == 0 ? endA : endB;
  if (length(vec2(c.pos.x - tgt.x, c.pos.z - tgt.z)) < 450.f) c.wp ^= 1;
  if (fabsf(c.bank) > 100.f * DEG) c.bank = wrapPi(c.bank);   // rolling out from inverted
  updateFlight(c, dt, tgt, 80.f, 60.f * DEG);
  c.timer -= dt;
  float lineHdg = atan2f(tgt.x - box.x, -(tgt.z - box.z));
  float lineErr = fabsf(wrapPi(c.hdg - lineHdg));
  if (c.timer <= 0 && agl > 360.f && fabsf(c.bank) < 0.2f && lineErr < 0.3f && length(vec2(c.pos.x - box.x, c.pos.z - box.z)) < 1100.f) {
    int m = (int)rnd(0, kManCount - 0.01f);
    if ((m == 4 || m == 5) && agl < 560.f) m = 3;   // split-S / Cuban eight need height
    c.man = m; c.seg = 0; c.segT = 0; c.smoke = true;
  }
}

// ------------------------------------------------------------------ escort: the O+P display pair
// The show is choreographed in a frame that rides with the player (x right, y up, z along the flight path), so the
// pair always performs around the player whatever they do. Attitude comes from the path itself: the jets bank and
// pull exactly as the curve demands (lift along the needed acceleration), with scripted rolls added on top.
namespace {
struct Key { float t, x, y, z, roll; };
// cubic Hermite between two points with end velocities, u in 0..1 over duration T
vec3 hermite(vec3 p0, vec3 v0, vec3 p1, vec3 v1, float u, float T) {
  float u2 = u * u, u3 = u2 * u;
  return p0 * (2 * u3 - 3 * u2 + 1) + v0 * (T * (u3 - 2 * u2 + u)) + p1 * (-2 * u3 + 3 * u2) + v1 * (T * (u3 - u2));
}
// Catmull-Rom through timed keys (zero velocity at the ends); x is mirrored by side, z scaled by zs
vec3 keyPath(const Key* k, int n, float t, float side, float zs, float* roll) {
  auto P = [&](int i) { return vec3(k[i].x * side, k[i].y, k[i].z * (k[i].z > 0.f || k[i].z < -60.f ? zs : 1.f)); };   // far points stretch with speed
  t = clampf(t, k[0].t, k[n - 1].t);
  int i = 0; while (i < n - 2 && t > k[i + 1].t) i++;
  auto V = [&](int j) { if (j <= 0 || j >= n - 1) return vec3(0, 0, 0); return (P(j + 1) - P(j - 1)) * (1.f / (k[j + 1].t - k[j - 1].t)); };
  float T = k[i + 1].t - k[i].t, u = (t - k[i].t) / T;
  if (roll) { float a = u * u * (3 - 2 * u); *roll = (k[i].roll + (k[i + 1].roll - k[i].roll) * a) * DEG * side; }
  return hermite(P(i), V(i), P(i + 1), V(i + 1), u, T);
}
const vec3 kSlot(38.f, 5.f, 45.f);   // a little ahead of the player's wingline, where both views can see it
const Key kCross[] = {{0, 38, 5, 45, 0}, {3, 110, 25, 350, 0}, {5, 150, 30, 700, 0}, {6.2f, 230, 32, 790, 0}, {7.4f, 310, 30, 700, 0},
                      {8.6f, 210, 22, 380, 0}, {9.6f, 70, 9, 120, 90}, {10.2f, 28, 5, 0, 90}, {10.9f, 22, 3, -230, 0},
                      {12.3f, 22, 130, -420, 0}, {13.6f, 25, 250, -330, 0}, {15.6f, 40, 110, -140, 0}, {19, 38, 5, 45, 0}};
const Key kSplit[] = {{0, 38, 5, 45, 0}, {2.5f, 90, 70, 120, 0}, {5, 260, 200, 380, 0}, {7.5f, 380, 120, 600, 0}, {9.5f, 250, 40, 520, 0},
                      {11.5f, 0, 6, 260, 0}, {13.5f, -200, 20, 150, 0}, {16, -90, 10, 20, 0}, {18, -38, 5, 45, 0}};
const Key kLeave[] = {{0, 38, 5, 45, 0}, {1, 38, 5, 45, 25}, {1.5f, 38, 5, 45, -25}, {2, 38, 5, 45, 25}, {2.5f, 38, 5, 45, -25},
                      {3, 38, 5, 45, 0}, {6, 250, 300, 500, 180}, {9, 900, 1100, 2500, 360}, {13, 1800, 2200, 5000, 360}};
}

float Traffic::escDur(int act) const {
  switch (act) {
    case ESC_JOIN: return 10.f;
    case ESC_FORM: return 10.f;
    case ESC_HELIX: return 16.f;
    case ESC_CROSS: return 19.f;
    case ESC_LOOPS: return 16.f;
    case ESC_SPLIT: return 18.f;
    default: return 13.f;
  }
}

vec3 Traffic::escRel(int act, float t, float side, float* roll, bool* smoke) const {
  float zs = clampf(escV / 70.f, 1.f, 4.5f);
  vec3 slot(kSlot.x * side, kSlot.y, kSlot.z);
  *roll = 0; *smoke = true;
  switch (act) {
    case ESC_JOIN: {   // run in from far behind and above, slowing into the slots
      float u = clampf(t / 10.f, 0, 1), e = 1.f - (1.f - u) * (1.f - u);
      vec3 st(700.f * side, 220.f, -1500.f * zs);
      *smoke = u > 0.85f;
      return st + (slot - st) * e + vec3(0, 60.f * sinf(PI * e) * (1.f - e), 0);
    }
    case ESC_FORM: {   // close formation, a synchronised roll each (mirrored), gentle bob
      float r1 = smoothstepf(3.f, 5.5f, t), r2 = smoothstepf(6.5f, 9.f, t);
      *roll = (r1 - r2) * 2 * PI * side;
      return slot + vec3(0, 2.5f * sinf(t * 0.9f) * smoothstepf(0, 1, t) * smoothstepf(10, 9, t), 0);
    }
    case ESC_HELIX: {  // a double helix around the player's flight path, the pair on opposite sides
      float D = 16.f, b = smoothstepf(0, 2.5f, t) * smoothstepf(D, D - 2.5f, t);
      float R = 38.f + (60.f * sqrtf(zs) - 38.f) * b;
      float th = (side > 0 ? 0.f : PI) + 3 * 2 * PI * smoothstepf(0.5f, D - 0.5f, t);
      return vec3(R * cosf(th), 5.f + R * sinf(th), 45.f + 30.f * zs * b);
    }
    case ESC_CROSS: return keyPath(kCross, sizeof(kCross) / sizeof(Key), t, side, zs, roll);
    case ESC_LOOPS: {  // break ahead and loop side by side, then drop back into the slots
      float R = std::max(140.f, (escV + 110.f) * 7.f / (2 * PI)), vl = 2 * PI * R / 7.f, Zl = 120.f * zs;
      vec3 L0(60.f * side, 10.f, Zl);
      if (t < 3.f) return hermite(slot, vec3(0, 0, 0), L0, vec3(0, 0, vl), t / 3.f, 3.f);
      if (t < 10.f) { float ps = 2 * PI * (t - 3.f) / 7.f; return vec3(L0.x, 10.f + R - R * cosf(ps), Zl + R * sinf(ps)); }
      return hermite(L0, vec3(0, 0, vl), slot, vec3(0, 0, 0), clampf((t - 10.f) / 6.f, 0, 1), 6.f);
    }
    case ESC_SPLIT: {  // pull up and out, swing round and cross in front of the player (one high, one low)
      vec3 p = keyPath(kSplit, sizeof(kSplit) / sizeof(Key), t, side, zs, roll);
      p.y += side * 14.f * smoothstepf(9.5f, 11.5f, t) * smoothstepf(13.5f, 11.5f, t);
      return p;
    }
    default: {         // farewell: rock the wings, then pull up and away in reheat
      vec3 p = keyPath(kLeave, sizeof(kLeave) / sizeof(Key), t, side, zs, roll);
      *smoke = t > 3.f;
      return p;
    }
  }
}

void Traffic::spawnEscort(vec3 player, vec3 playerVel) {
  craft.erase(std::remove_if(craft.begin(), craft.end(), [](const TrafficCraft& c) { return c.role == TrafficCraft::ESCORT; }), craft.end());
  vec3 f = playerVel; f.y = 0;
  escF = length(f) > 5.f ? normalize(playerVel) : vec3(0, 0, -1);
  escV = std::max(length(playerVel), 40.f); escLift = 0;
  escAct = ESC_JOIN; escT = 0; escNext = 0; escLeft = 0; escortStop = false;
  for (int j = 0; j < 2; j++) {
    TrafficCraft c; c.id = nextId++;
    c.spec = kResearchJet; c.role = TrafficCraft::ESCORT; c.state = TrafficCraft::FLY;
    c.escSide = j ? -1.f : 1.f; c.escUp = vec3(0, 1, 0);
    c.colBase = j ? vec3(0.06f, 0.16f, 0.62f) : vec3(0.72f, 0.07f, 0.05f);   // display liveries: red / gold and blue / white
    c.colStripe = j ? vec3(0.95f, 0.95f, 1.f) : vec3(1.f, 0.78f, 0.15f);
    c.smokeCol = j ? vec3(0.3f, 0.55f, 1.f) : vec3(1.f, 0.3f, 0.2f);
    c.gear = 0; c.flaps = 0; c.throttle = 0.8f;
    vec3 R = normalize(cross(escF, vec3(0, 1, 0)) + vec3(1e-4f, 0, 0)), U = cross(R, escF);
    float roll; bool sm;
    vec3 rel = escRel(ESC_JOIN, 0, c.escSide, &roll, &sm);
    c.pos = player + R * rel.x + U * rel.y + escF * rel.z;
    c.vel = escF * escV; c.speed = escV;
    c.hdg = atan2f(escF.x, -escF.z); attitudeToQuat(c);
    craft.push_back(c);
  }
  radio.push_back("SPECTRE DISPLAY PAIR: Two Specters joining on your wing - enjoy the show!");
}

// an act change mid-manoeuvre: each jet blends from where it is onto the new act's path
void Traffic::escMark() {
  for (auto& c : craft) {
    if (c.role != TrafficCraft::ESCORT) continue;
    float r; bool sm;
    vec3 st = escRel(escAct, 0, c.escSide, &r, &sm);
    c.escFrom = c.escRel; c.escBlend = length(st - c.escRel) > 5.f ? 1.f : 0.f;
  }
}

int Traffic::rayHit(vec3 a, vec3 d, float len, float& tHit) const {
  int best = -1; tHit = len;
  for (int i = 0; i < (int)craft.size(); i++) {
    const TrafficCraft& c = craft[i];
    if (!c.alive || c.role == TrafficCraft::ESCORT) continue;
    float r = std::max(kAircraft[c.spec].span, kAircraft[c.spec].fusLen) * 0.45f;
    float t = dot(c.pos - a, d);
    if (t < 0 || t > tHit) continue;
    if (length(c.pos - (a + d * t)) < r) { tHit = t; best = i; }
  }
  return best;
}

int Traffic::destroyNear(vec3 c, float R, std::vector<vec3>& where) {
  int n = 0;
  for (auto& k : craft) if (k.alive && k.role != TrafficCraft::ESCORT && length(k.pos - c) < R) { k.alive = false; where.push_back(k.pos); n++; }
  return n;
}

void Traffic::dismissEscort() {
  if (count(TrafficCraft::ESCORT) == 0 || escAct == ESC_LEAVE) return;
  escAct = ESC_LEAVE; escT = 0; escMark();
  radio.push_back("SPECTRE: That's the show - Specters breaking off. Fly safe!");
}

void Traffic::updateEscorts(float dt, vec3 player, vec3 playerVel) {
  if (count(TrafficCraft::ESCORT) == 0) return;
  // the player's frame, smoothed against turbulence: flight-path direction (with climb), right, up
  if (length(playerVel) > 5.f) escF = normalize(escF + (normalize(playerVel) - escF) * (1.f - expf(-2.5f * dt)));
  escV += (std::max(length(playerVel), 40.f) - escV) * (1.f - expf(-1.5f * dt));
  vec3 F = escF, R = normalize(cross(F, vec3(0, 1, 0)) + vec3(1e-4f, 0, 0)), U = cross(R, F);
  // keep the show clear of the ground when the player is low
  float agl = player.y - std::max(g_world.height(player.x, player.z), 0.f);
  escLift += (clampf(130.f - agl, 0.f, 200.f) - escLift) * (1.f - expf(-1.f * dt));
  // act sequencing: an act can only change at its end, when both jets are back in their slots
  escT += dt;
  if (escortStop && escAct != ESC_LEAVE && (escAct == ESC_FORM || escT >= escDur(escAct))) {
    escAct = ESC_LEAVE; escT = 0; escMark(); radio.push_back("SPECTRE: You're on approach - we'll leave you to it. Specters out!");
  }
  if (escT >= escDur(escAct)) {
    if (escAct == ESC_LEAVE) { for (auto& c : craft) if (c.role == TrafficCraft::ESCORT) c.alive = false; return; }
    if (escAct == ESC_SPLIT) for (auto& c : craft) if (c.role == TrafficCraft::ESCORT) c.escSide = -c.escSide;   // they crossed over
    static const int order[] = {ESC_FORM, ESC_HELIX, ESC_CROSS, ESC_FORM, ESC_LOOPS, ESC_SPLIT};
    static const char* calls[] = {"SPECTRE: Smoke on... now. Tight formation, hold her steady.", "SPECTRE: Helix - we'll wrap you up. Don't flinch!",
                                  "SPECTRE: Opposition pass coming at you, knife-edge!", "SPECTRE: Back in formation. Nice flying, partner.",
                                  "SPECTRE: Synchronised loops off your nose - watch this.", "SPECTRE: Split and cross - one high, one low!"};
    escAct = order[escNext % 6]; radio.push_back(calls[escNext % 6]); escNext++;
    escT = 0; escMark();
  }
  const float h = 0.05f;
  for (auto& c : craft) {
    if (c.role != TrafficCraft::ESCORT || !c.alive) continue;
    float roll, roll2; bool smoke, sm2;
    auto relAt = [&](float t, float* rl, bool* sm) {
      vec3 p = escRel(escAct, t, c.escSide, rl, sm);
      if (c.escBlend > 0.f) { float w = smoothstepf(0.f, 3.f, t); p = c.escFrom + (p - c.escFrom) * w; }
      return p;
    };
    vec3 rel = relAt(escT, &roll, &smoke);
    vec3 r0 = relAt(std::max(escT - h, 0.f), &roll2, &sm2), r1 = relAt(escT + h, &roll2, &sm2);
    c.escRel = rel;
    vec3 relV = (r1 - r0) * (1.f / (2 * h)), relA = (r1 - rel * 2.f + r0) * (1.f / (h * h));
    rel.y += escLift;
    auto W = [&](vec3 v) { return R * v.x + U * v.y + F * v.z; };
    vec3 pos = player + W(rel), vel = F * escV + W(relV), acc = W(relA);
    float sp = std::max(length(vel), 1.f);
    vec3 f = vel * (1.f / sp);
    // lift along the acceleration the path needs (plus holding up against gravity); roll towards it at a finite rate
    vec3 lift = acc + vec3(0, G, 0); lift = lift - f * dot(lift, f);
    vec3 up = c.escUp - f * dot(c.escUp, f);
    up = length(up) > 1e-3f ? normalize(up) : U;
    if (length(lift) > 0.5f) {
      vec3 ld = normalize(lift);
      float ang = atan2f(dot(cross(up, ld), f), dot(up, ld));
      float stepA = clampf(ang, -6.f * dt, 6.f * dt);
      up = up * cosf(stepA) + cross(f, up) * sinf(stepA);
    }
    c.escUp = up;
    vec3 upR = up * cosf(roll) + cross(f, up) * sinf(roll);   // scripted rolls (positive = to the right)
    vec3 rx = normalize(cross(f, upR)); upR = cross(rx, f);
    c.q = quatFromBasis(rx, upR, f * -1.f);   // body +x right, +y up, +z back
    c.hdg = atan2f(f.x, -f.z); c.pitch = asinf(clampf(f.y, -1.f, 1.f)); c.bank = 0;
    c.pos = pos; c.vel = vel; c.speed = sp; c.vs = vel.y;
    c.smoke = smoke;
    c.ab = sp > 260.f || (escAct == ESC_LEAVE && escT > 3.f) ? 1.f : 0.f;
    c.throttle = clampf(0.5f + dot(acc, f) * 0.02f + c.ab * 0.5f, 0.3f, 1.f);
    c.ctlPitch = clampf(dot(acc, upR) * 0.01f, -1.f, 1.f); c.ctlRoll = 0; c.ctlYaw = 0;
    c.timer += dt;
  }
}

// ------------------------------------------------------------------ update
bool Traffic::update(float dt, vec3 player, vec3 playerVel, bool playerOnGround, float playerSpan) {
  puffs.clear(); booms.clear(); flybys.clear(); radio.clear();
  if (!enabled)   // traffic off in the settings: only a summoned display pair flies
    craft.erase(std::remove_if(craft.begin(), craft.end(), [](const TrafficCraft& c) { return c.role != TrafficCraft::ESCORT; }), craft.end());
  t += dt;
  bool hitPlayer = false;
  updateEscorts(dt, player, playerVel);
  // airport traffic around nearby airports
  spawnT -= dt;
  if (enabled && spawnT <= 0) {
    spawnT = 2.f;
    for (int ai = 0; ai < (int)g_world.airports.size(); ai++) {
      const Airport& a = g_world.airports[ai];
      if (!apUsable(a)) continue;
      float d = length(vec2(a.x - player.x, a.z - player.z));
      int want = d < 14000.f ? (a.size == 2 ? 4 : 2) : 0, have = 0;
      for (auto& c : craft) if (c.alive && c.role == TrafficCraft::AIRPORT && c.airport == ai) have++;
      if (have < want) spawnAirport(ai, player);
    }
    if (count(TrafficCraft::CRUISER) < 3) spawnCruiser(player);
    // the display team performs over the nearest airport with an apron
    if (count(TrafficCraft::STUNT) == 0) {
      int best = -1; float bd = 15000.f;
      for (int ai = 0; ai < (int)g_world.airports.size(); ai++) {
        const Airport& a = g_world.airports[ai];
        float d = length(vec2(a.x - player.x, a.z - player.z));
        if (apUsable(a) && d < bd) { bd = d; best = ai; }
      }
      if (best >= 0) spawnDisplayTeam(best);
    }
  }
  formationT -= dt;
  if (enabled && formationT <= 0) { formationT = rnd(80.f, 200.f); spawnFormation(player, playerVel); }

  for (size_t i = 0; i < craft.size(); i++) {
    TrafficCraft& c = craft[i];
    if (!c.alive) continue;
    const AircraftSpec& s = kAircraft[c.spec];
    c.propAngle = fmodf(c.propAngle + (8.f + 50.f * c.throttle) * dt, 2 * PI * 50.f);
    float dist = length(c.pos - player);
    if (c.leader >= 0) {   // wingman: rigid formation on the leader
      TrafficCraft& L = craft[c.leader];
      if (!L.alive) { c.leader = -1; c.man = -1; }
      else {
        c.q = L.q; c.pos = L.pos + L.q.rotate(c.offset); c.vel = L.vel; c.speed = L.speed;
        c.hdg = L.hdg; c.pitch = L.pitch; c.bank = L.bank; c.smoke = L.smoke;
        c.ctlPitch = L.ctlPitch; c.ctlRoll = L.ctlRoll; c.ctlYaw = L.ctlYaw; c.throttle = L.throttle; c.ab = L.ab;
      }
    } else switch (c.role) {
      case TrafficCraft::AIRPORT:
        updateAirport(c, dt, player, playerOnGround);
        if (length(vec2(g_world.airports[c.airport].x - player.x, g_world.airports[c.airport].z - player.z)) > 20000.f) c.alive = false;
        break;
      case TrafficCraft::CRUISER:
        updateFlight(c, dt, c.path.empty() ? c.pos + c.vel : c.path[0], s.cruise, 20.f * DEG);
        if (dist > 18000.f) c.alive = false;
        break;
      case TrafficCraft::FORMATION: {
        // straight high-speed pass; once well past the player, pull up into a climbing roll
        vec3 f = c.q.rotate(vec3(0, 0, -1));
        c.timer += dt;
        vec3 rel = player - c.pos;
        if (c.man == 0 && dot(rel, f) < -1500.f) c.man = 1;
        if (c.man == 1) {
          float pitchNow = asinf(clampf(f.y, -1, 1));
          if (pitchNow < 55.f * DEG) c.q = c.q * quat::axisAngle(vec3(1, 0, 0), 0.5f * dt);
          c.segT += dt;
          if (c.segT < 1.6f) c.q = c.q * quat::axisAngle(vec3(0, 0, -1), 2 * PI / 1.6f * dt);
          c.q.normalize();
          c.ctlPitch = 0.6f;
        }
        f = c.q.rotate(vec3(0, 0, -1));
        c.vel = f * c.speed; c.pos += c.vel * dt;
        c.boomCD -= dt;
        if (c.speed > 340.f && dist < 1300.f && c.boomCD <= 0) { booms.push_back({c.pos, clampf(1.3f - dist / 1300.f, 0.25f, 1.f)}); c.boomCD = 30.f; }
        if (dist > 20000.f || c.timer > 120.f) c.alive = false;
        break;
      }
      case TrafficCraft::ESCORT: {   // flown by updateEscorts; here: booms and fly-by roars on close fast passes
        c.boomCD -= dt; c.flybyCD -= dt;
        if (c.speed > 340.f && dist < 1500.f && c.boomCD <= 0) { booms.push_back({c.pos, clampf(1.3f - dist / 1500.f, 0.25f, 1.f)}); c.boomCD = 25.f; }
        vec3 relV = c.vel - playerVel, toP = player - c.pos;
        float closing = dot(relV, toP) / std::max(dist, 1.f);
        if (dist < 220.f && closing > 25.f && c.flybyCD <= 0) { flybys.push_back(clampf(closing / 200.f, 0.35f, 1.f) * clampf(1.4f - dist / 220.f, 0.4f, 1.f)); c.flybyCD = 5.f; }
        break;
      }
      case TrafficCraft::STUNT:
        updateStunt(c, dt);
        if (length(vec2(g_world.airports[c.airport].x - player.x, g_world.airports[c.airport].z - player.z)) > 22000.f) c.alive = false;
        break;
    }
    // ground safety for anything flying
    float g = g_world.height(c.pos.x, c.pos.z);
    if (c.role != TrafficCraft::AIRPORT && c.pos.y < g + 30.f) { c.pos.y = g + 30.f; if (c.vel.y < 0) c.vel.y = 0; }
    // effects: display smoke, reheat embers
    if (c.smoke) {   // display smoke, spread along this frame's path so it draws a continuous ribbon
      int n = (int)clampf(c.speed * dt / 2.5f, 1.f, 6.f);
      for (int k = 0; k < n; k++)
        puffs.push_back({c.pos + c.q.rotate(vec3(0, 0, s.fusLen * 0.55f)) - c.vel * (dt * k / n), vec3(0, 0.3f, 0), c.smokeCol, 8.f, 1.2f, 1.3f, 0.5f, SPR_SMOKE});
    }
    if (c.ab > 0.5f && dist < 6000.f)
      for (int k = -1; k <= 1; k += 2) puffs.push_back({c.pos + c.q.rotate(vec3(k * 0.82f, -0.12f, 9.2f)), c.vel * 0.7f, vec3(1.f, 0.6f, 0.25f) * 2.5f, 0.12f, 0.6f, -1.f, 1.f, SPR_SPARK});
    // mid-air collision with the player
    if (dist < (playerSpan + s.span) * 0.32f && !playerOnGround && c.role != TrafficCraft::AIRPORT && c.role != TrafficCraft::ESCORT) hitPlayer = true;
    if (dist < (playerSpan + s.span) * 0.32f && !playerOnGround && c.role == TrafficCraft::AIRPORT && c.state >= TrafficCraft::CLIMB && c.state <= TrafficCraft::FINAL) hitPlayer = true;
  }
  craft.erase(std::remove_if(craft.begin(), craft.end(), [](const TrafficCraft& c) { return !c.alive; }), craft.end());
  // leader indices shift when craft are removed: re-link wingmen by finding the craft spawned just before them
  for (size_t i = 0; i < craft.size(); i++) {
    TrafficCraft& c = craft[i];
    if (c.role != TrafficCraft::FORMATION && c.role != TrafficCraft::STUNT) continue;
    if (c.leader >= 0) {
      int L = -1;
      for (int j = (int)i - 1; j >= 0; j--) if (craft[j].role == c.role && craft[j].leader < 0) { L = j; break; }
      c.leader = L;
    }
  }
  return hitPlayer;
}

int Traffic::fillVisuals(vec3 camPos, TrafficVisual* out, int maxN, int* order) const {
  std::vector<std::pair<float, int>> d;
  for (int i = 0; i < (int)craft.size(); i++) {
    float dist = length(craft[i].pos - camPos);
    if (dist < 16000.f) d.push_back({dist, i});
  }
  std::sort(d.begin(), d.end());
  int n = std::min((int)d.size(), maxN);
  for (int k = 0; k < n; k++) {
    const TrafficCraft& c = craft[d[k].second];
    if (order) order[k] = d[k].second;
    const AircraftSpec& s = kAircraft[c.spec];
    float* t = out[k].t;
    packModel(s, c.spec, gearH(s), t);
    float bound = std::max(t[0], t[36] * 2.f) * 0.55f + 1.5f;
    vec3 r = c.q.rotate(vec3(1, 0, 0)), u = c.q.rotate(vec3(0, 1, 0)), b = c.q.rotate(vec3(0, 0, 1));
    float v[32] = {c.pos.x, c.pos.y, c.pos.z, bound, r.x, r.y, r.z, 0, u.x, u.y, u.z, 0, b.x, b.y, b.z, 0,
                   c.spec == kResearchJet ? 0.f : c.gear, c.flaps, 0, 0, c.ctlPitch, c.ctlRoll, c.ctlYaw, c.throttle,
                   c.colBase.x, c.colBase.y, c.colBase.z, c.propAngle, c.colStripe.x, c.colStripe.y, c.colStripe.z, c.ab};
    for (int i = 0; i < 32; i++) t[96 + i] = v[i];
  }
  return n;
}
