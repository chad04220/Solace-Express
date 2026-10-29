// Somnium Volandi - the weather in flight: the clouds and their drift, the wake through them, the sun, lightning
#include "road_traffic.h"
#include "volcano_effects.h"
#include "game.h"
#include "earth_charts.h"
#include "sky.h"
#include "load_pacer.h"
#include "world_load_progress.h"
#include <chrono>
#include <ctime>
#include <thread>
#include <stdexcept>
#include "airport_layout.h"
#include "entities.h"
#include "entity_lod.h"
#include "hive_world_collision.h"
#include "scenery.h"
#include "regional_surface.h"
#include "models.h"
#include "menu_layout.h"
#include "hangar_catalog.h"
#include "military_missions.h"
#include "game_internal.h"

// The clouds move with the wind (weather.h): the field drifts at twice the surface wind, its fine detail a little
// faster still (the wisps stream off the clouds' downwind edges) and the billows rise through it, quickly in a
// sunny day's cumulus, hardly at all in a grey overcast. wx carries them, so the bumps, the rain and the icing are
// where the clouds are drawn.
void Game::moveClouds(float dt) {
  // (the offsets are added to the position the field is read at, so the clouds move against them: adding the wind
  // had them drifting into it)
  const vec3 d(-sinf(wx.windFrom * DEG), 0, cosf(wx.windFrom * DEG));
  cloudOff = cloudOff - vec2(d.x, d.z) * (wx.windSpeed * 2.f * dt);
  cloudDet += (vec3(0, 1.4f, 0) - d * (wx.windSpeed * 0.6f)) * dt;
  const float cu = smoothstepf(0.08f, 0.22f, wx.cloudCover) * smoothstepf(0.9f, 0.6f, wx.cloudCover);
  cloudBoil += (0.3f + 1.4f * cu + (wx.storm ? 2.f : 0.f)) * dt;
  wx.cloudDrift = cloudOff; wx.cloudDetail = cloudDet; wx.cloudBoil = cloudBoil;
}
Game::WakePt Game::wakeHere(vec3 c, bool brk, float odo) const {
  // the pair the airframe's surfaces are rolling up (aero_wake.h), which sinks at w0 = G / (2 pi b0) under its own
  // downwash and slows as it decays over a few times b0 / w0. Without one (too slow to fly), a lifting line's: the
  // weight times the load shed round two cores pi/4 of the span apart
  const AeroWake& wk = plane.wake;
  float gam = wk.gamPair, b0 = wk.b0, py = wk.pairY;
  if (b0 < 0.1f) {
    b0 = 0.25f * PI * plane.spec->span;
    const float rho = 1.225f * powf(std::max(1.f - 2.2558e-5f * plane.pos.y, 0.3f), 4.2559f);
    gam = plane.mass() * G0 * clampf(plane.gLoad, -6.f, 12.f) / (rho * std::max(plane.airspeed, 20.f) * b0);
    py = plane.spec->wingY;
  }
  const float w0 = gam / (2.f * PI * b0);
  // the bank: the airframe's x axis against the path's level across, seen from behind
  const vec3 f = normalize(plane.vel - plane.windVel + vec3(0, 0, 1e-4f)), sd = normalize(cross(f, vec3(0, 1, 0)) + vec3(1e-5f, 0, 0)), upv = cross(sd, f);
  const vec3 bx = plane.q.rotate(vec3(1, 0, 0));
  return {c, 0.f, brk, w0, clampf(4.f * b0 / std::max(fabsf(w0), 0.05f), 20.f, 120.f), odo, gam, b0, atan2f(dot(bx, upv), dot(bx, sd)), py};
}
// The aircraft came back in over the map's seam (Plane::step, world.h WRAP_HALF): what the flight has put in the world
// round it comes with it - the camera, the smoke and the sparks, the wreckage, the wingtip ribbons, its path through the
// cloud, the traffic flying with it, the renderer's last frame - so nothing jumps. What belongs to a place (the
// craters, the fields' own traffic, the route) stays: across the seam the map is the same.
void Game::followSeam(vec3 d) {
  g_audio.shiftWorld(d); audioCameraPrevious += d;
  camPos += d; dbgCamPos += d; dbgCamLook += d;
  for (auto& p : particles) p.p += d;
  for (auto& w : wakeRings) w.p += d;
  for (auto& w : wreck) w.c += d;
  for (auto& b : debris) b.p += d;
  for (auto& p : pops) p.p += d;
  for (auto& tr : pieceTrail) for (auto& t : tr) { t.p += d; t.sw.c += d; }
  for (auto& tr : tipTrail) for (auto& t : tr) { t.p += d; t.sw.c += d; }
  boomP += d;
  for (auto& w : cloudWake) w.c += d;
  ufo.pos += d;
  for (auto& b : somnus.bolts) b.h += d;
  for (auto& b : somnus.bombs) b.p += d;
  for (auto& b : somnus.blasts) b.p += d;
  somnus.cam.pos += d; somnus.cam.look += d; somnus.cam.blastP += d;
  traffic.followSeam(d);
  g_ren.followSeam(d);
}

// The aircraft through the cloud: its path in the cloud layer is kept (a point every 150 m, or sooner in a turn) in
// the cloud field's frame, with the vortex pair as it was there, and the cloud pass stirs and clears a channel along
// it that opens a moment behind the aircraft, widens and fills in again over the next minute (FrameParams::wake). Flying in or near cloud, wisps of it stream
// past (their parallax is the speed you feel in cloud), and the wingtips' vortices draw ribbons through it.
void Game::updateCloudWake(float dt) {
  // (the rain and the cloud at the aircraft, smoothed: the windscreen, the rain round the camera, the visibility, the drumming on the airframe)
  rainNow = flightClock < 0.1f ? plane.wxl.rain : approach(rainNow, plane.wxl.rain, 1.2f, dt);   // (raining already as the flight opens)
  float mist = clampf(plane.wxl.cloud * 2.f, 0.f, 0.8f) * smoothstepf(5.f, 30.f, plane.airspeed);
  mistNow = approach(mistNow, mist, mist > mistNow ? 3.f : 0.4f, dt);
  for (auto& w : cloudWake) w.age += dt;
  while (!cloudWake.empty() && cloudWake.front().age > 75.f) cloudWake.erase(cloudWake.begin());
  const float top = wx.cloudBase + wxfield::cloudThickness(wx);
  const bool inLayer = wx.cloudCover >= 0.02f && !crashed && plane.spec && plane.airspeed > 12.f && plane.pos.y > wx.cloudBase - 80.f && plane.pos.y < top + 80.f;
  const vec3 c = plane.pos + vec3(cloudOff.x, 0, cloudOff.y);
  if (inLayer) {
    bool push = cloudWake.empty();
    if (!push) {
      const WakePt& L = cloudWake.back();
      float dd = length(c - L.c);
      bool turned = false;
      if (cloudWake.size() >= 2 && !L.brk && dd > 1.f) { const WakePt& P = cloudWake[cloudWake.size() - 2]; turned = dot(normalize(c - L.c), normalize(L.c - P.c)) < 0.995f; }
      push = dd > 150.f || (dd > 30.f && turned) || L.age > 2.5f;
    }
    if (push) {
      if (!wakeGap && !cloudWake.empty()) wakeOdo += length(c - cloudWake.back().c);
      cloudWake.push_back(wakeHere(c, wakeGap, wakeOdo));
      wakeGap = false;
    }
    while (cloudWake.size() > (size_t)FrameParams::kWakeMax - 1) cloudWake.erase(cloudWake.begin());
  } else wakeGap = true;
  // wisps: the cloud just ahead, as puffs that stay in the air and stream past
  if (!plane.spec || crashed || plane.onGround) return;
  const float ahead = std::max(plane.wxl.cloud, wxfield::cloudDensity(wx, plane.pos + plane.vel * 0.7f, true));
  if (ahead > 0.02f && plane.airspeed > 15.f) {
    wispAccum += length(plane.vel) * dt * std::min(ahead * 3.f, 1.f) / 9.f;   // (one every 9 m flown in thick cloud)
    const float span = plane.spec->span;
    Rng& r = sparkRng;
    vec3 f = normalize(plane.vel), sd = normalize(cross(f, vec3(0, 1, 0))), upv = cross(sd, f);
    for (; wispAccum >= 1.f; wispAccum -= 1.f) {
      float ang = r.range(0.f, 2.f * PI), rad = span * r.range(0.45f, 2.6f);
      vec3 p = plane.pos + f * r.range(25.f, 70.f) + (sd * cosf(ang) + upv * sinf(ang)) * rad;
      float dens = wxfield::cloudDensity(wx, p, true);
      if (dens < 0.02f) continue;
      float g = r.range(0.86f, 0.97f);
      const size_t n0 = particles.size();
      spawn(p, plane.windVel, r.range(1.6f, 2.6f), r.range(4.f, 9.f), r.range(1.5f, 3.f), vec3(g, g, g * 1.02f), clampf(0.04f + dens * 0.14f, 0.f, 0.16f), SPR_SMOKE, 0.f, 0.f);
      if (particles.size() > n0) particles.back().air = true;   // (the airframe's flow carries it: updateParticles)
    }
  } else wispAccum = 0;
}
// Dynamic weather (C8): the conditions drift from the contract's weather to its forecast over the flight's estimated
// time (and a little beyond: the front keeps moving), with a slow wander on top; everything that reads wx (the flight
// model, the tower, the HUD) sees the current state. On the autopilot, a wind that has swung to favour the other
// runway end re-plans the approach while there is still room.
void Game::updateWeather(float dt) {
  if (researchFlight || !contract.wxShift) return;
  float T = std::max(launchPlan.minutesEst * 60.f, 120.f) * 1.1f;
  float f = smoothstepf(0.f, 1.f, clampf((jobClockBase + flightClock) / T, 0.f, 1.f));
  const Weather& a = wxStart; const Weather& b = contract.wxEnd;
  float wander = 6.f * sinf(gameTime * 0.011f) + 3.f * sinf(gameTime * 0.037f + 1.f);
  wx.windFrom = wrapDeg360(a.windFrom + wrapAngle((b.windFrom - a.windFrom) * DEG) / DEG * f + wander * f);
  wx.windSpeed = a.windSpeed + (b.windSpeed - a.windSpeed) * f + 0.4f * sinf(gameTime * 0.023f) * f;
  wx.gust = a.gust + (b.gust - a.gust) * f;
  wx.turbulence = a.turbulence + (b.turbulence - a.turbulence) * f;
  wx.cloudCover = a.cloudCover + (b.cloudCover - a.cloudCover) * f;
  wx.cloudBase = a.cloudBase + (b.cloudBase - a.cloudBase) * f;
  wx.visibility = a.visibility + (b.visibility - a.visibility) * f;
  if (f > 0.5f && (wx.precip != b.precip || wx.storm != b.storm)) { wx.precip = b.precip; wx.storm = b.storm; toast(b.precip == 2 ? "Snow has set in" : b.precip == 1 ? "Rain has started" : "The rain has stopped", vec3(0.8f, 0.8f, 0.8f)); }
  apRepickT = std::max(0.f, apRepickT - dt);
  if (plane.apOn && (plane.apMode == Plane::AP_NAV || plane.apMode == Plane::AP_APPR) && plane.apStage == Plane::APS_NAV && plane.apAirport >= 0 && apRepickT <= 0.f) {
    const Airport& A = g_world.airports[plane.apAirport];
    float hw = cosf((wx.windFrom - A.heading) * DEG) * wx.windSpeed, hwR = -hw;   // headwind component on each end (m/s)
    bool better = plane.apRev ? hw > hwR + 2.5f : hwR > hw + 2.5f;   // the other end has 5 kt more headwind
    float dA = length(vec3(plane.pos.x - A.pos().x, 0, plane.pos.z - A.pos().z));
    if (better && dA > 6000.f) {   // (the planner weighs the wind against the flying round: it may keep the end it has)
      bool was = plane.apRev;
      plane.apEngage(plane.apMode, plane.apAirport, wx);
      apRepickT = plane.apRev != was ? 90.f : 8.f;
      if (!plane.apDecline.empty()) {   // (neither end is safe in the new wind: the hold it falls back to is said, and why)
        g_audio.trigger(SFX_AP_DISC);
        toast("Autopilot: the wind has shifted - unable to autoland at " + plane.apDecline + " - circling clear of the ground (it tries again as the wind changes)", vec3(1, 0.75f, 0.35f));
        toast("Any stick input hands control back", vec3(0.8f, 0.8f, 0.8f));
      } else if (plane.apRev != was) toast(fmt("Autopilot: the wind has shifted - now runway %02d at %s", A.rwyNumber(plane.apRev), A.code), vec3(0.6f, 1, 0.6f));
    }
  }
}
// ------------------------------------------------------------------ sun & sky
void Game::computeSun(float tod, vec3& dir, vec3& col, float& night, float h, float* dim) const {
  dir = sunDirectionAt(tod);   // (its path: sky.h)
  float y = dir.y;
  // the sunlight reaching h up (common.glsl sunLightAt, exactly): through the air above it, and from up there over a
  // horizon that has dipped below the level - an aircraft high over the dusk is still in the sun, and above the air
  // it is the sun's own white
  h = std::max(h, 0.f);
  const float R = planetRadius(), dens = expf(-h / 8000.f), dip = R > 0.f ? sqrtf(std::max(1.f - (R / (R + h)) * (R / (R + h)), 0.f)) : 0.f;
  const float lowest = R > 0.f ? (R + h) * sqrtf(std::max(1.f - y * y, 0.f)) - R : h;
  const float k = y >= 0.f ? dens : 2.f * expf(-std::max(lowest, 0.f) / 8000.f) - dens;
  float od = k / std::max(y * 1.4f + 0.08f, 0.02f);   // optical depth: positive and bounded (x50) once the sun is down
  col = vec3(expf(-0.10f * od * 0.45f), expf(-0.23f * od * 0.45f), expf(-0.56f * od * 0.45f)) * smoothstepf(-0.06f, 0.05f, y + dip);
  const float weather = (1.f - 0.75f * wx.cloudCover * wx.cloudCover) * (wx.storm ? 0.6f : 1.f);
  col = col * weather;
  if (dim) *dim = weather;
  night = smoothstepf(0.06f, -0.14f, y);
}

void Game::makeStrike(vec3 top, vec3 foot, bool ground, uint32_t seed) {
  Rng r(seed);
  Strike S; S.top = top; S.foot = foot; S.ground = ground;
  const float H = std::max(length(top - foot), 1.f);
  // a channel from a to b, broken `levels` times; each segment with its glow's half width and brightness
  auto channel = [&](vec3 a, vec3 b, int levels, float w, float k, float rough) {
    std::vector<vec3> P{a, b};
    for (int l = 0; l < levels; l++) {
      std::vector<vec3> Q; Q.reserve(P.size() * 2);
      for (size_t i = 0; i + 1 < P.size(); i++) {
        const vec3 d = P[i + 1] - P[i]; const float L = length(d);
        vec3 n = cross(d, vec3(r.range(-1, 1), r.range(-1, 1), r.range(-1, 1)));
        n = length(n) > 1e-4f ? normalize(n) : vec3(1, 0, 0);
        Q.push_back(P[i]); Q.push_back((P[i] + P[i + 1]) * 0.5f + n * (L * rough * r.range(-1.f, 1.f)));
      }
      Q.push_back(P.back()); P.swap(Q);
    }
    for (size_t i = 0; i + 1 < P.size(); i++) {
      S.a.push_back(P[i]); S.b.push_back(P[i + 1]); S.w.push_back(w); S.k.push_back(k);
      S.along.push_back(clampf(length((P[i] + P[i + 1]) * 0.5f - top) / H, 0.f, 1.f));
    }
    return P;
  };
  if (H > 2.f) {   // (a flash hidden in the cloud has no channel)
    const std::vector<vec3> M = channel(top, foot, 7, ground ? 6.f : 4.f, ground ? 1.f : 0.6f, 0.2f);
    S.main = (int)S.a.size();
    const vec3 down = normalize(foot - top);
    const int nb = 4 + (int)r.range(0.f, 6.f);
    for (int i = 0; i < nb; i++) {
      const int at = (int)r.range(4.f, (float)M.size() * 0.66f);
      const vec3 side = normalize(cross(down, normalize(vec3(r.range(-1, 1), r.range(-0.3f, 0.3f), r.range(-1, 1)) + vec3(1e-3f, 0, 0))));
      const float L = H * r.range(0.12f, 0.35f) * (1.f - (float)at / M.size());
      const vec3 to = M[at] + normalize(down * r.range(0.5f, 1.f) + side * r.range(0.4f, 1.1f)) * L;
      const std::vector<vec3> B = channel(M[at], to, 5, 3.f, 0.6f, 0.24f);
      if (r.uni() < 0.6f) {   // (and a twig off it)
        const int j = (int)r.range(1.f, B.size() * 0.6f);
        channel(B[j], B[j] + normalize(down + side * r.range(-1.2f, 1.2f)) * (L * r.range(0.25f, 0.5f)), 4, 1.8f, 0.4f, 0.26f);
      }
    }
  }
  S.strokes = 2 + (int)r.range(0.f, 2.99f);   // (2 to 4)
  float t0 = 0.045f;
  for (int i = 0; i < S.strokes; i++) { S.restrike[i] = t0; t0 += r.range(0.05f, 0.12f); }
  if (strikes.size() >= 3) strikes.erase(strikes.begin());
  strikes.push_back(std::move(S));
}
