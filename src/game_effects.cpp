// Somnium Volandi - effects: jet exhausts, lights, the UFO, lightning strikes, crash wreckage, particles
#include "road_traffic.h"
#include "dev_switches.h"
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

// research craft exhaust exits and jet directions (body coords): XR-30 two 2D nozzles, XR-40 four pods.
// strength: that exhaust's share of the thrust (1 = the XR-30's)
int jetExhausts(const Plane& p, vec3* pos, vec3* dir, float* strength) {
  if (p.spec->special == 3) {   // the XR-50: its two nozzles, swivelled and vectored, and the rocket between them while it burns
    int n = 0;
    for (int s = 0; s < 2; s++) {
      const float sd = s ? 1.f : -1.f, a = p.nzSwivel + p.nzPitch[s], y = p.nzYaw[s];
      pos[n] = vec3(sd * kMorpheusNozzle.x, kMorpheusNozzle.y, kMorpheusNozzle.z) + vec3(0, -sinf(p.nzSwivel), cosf(p.nzSwivel)) * 1.15f;
      dir[n] = normalize(vec3(-sinf(y), -sinf(a) * cosf(y), cosf(a) * cosf(y)));
      strength[n++] = 1.f;
    }
    if (p.rocketN > 1.f) { pos[n] = kMorpheusRocket; dir[n] = vec3(0, 0, 1); strength[n++] = clampf(p.rocketN / kMorpheusRocketVac, 0.f, 1.f); }
    return n;
  }
  if (p.spec->special == 2) {
    for (int i = 0; i < 4; i++) {
      float a0 = p.podTilt[i], a = a0 + p.podVane[i], y = p.podYaw[i];
      pos[i] = kSomnusPods[i] + vec3(0, -sinf(a0), cosf(a0)) * 1.55f;
      dir[i] = normalize(vec3(-sinf(y), -sinf(a) * cosf(y), cosf(a) * cosf(y)));
      strength[i] = clampf(p.podThr[i] * 1.4f, 0.f, 1.2f);
    }
    return 4;
  }
  float a = jetNozzleAngle(p);
  for (int s = 0; s < 2; s++) { pos[s] = vec3(s ? 0.82f : -0.82f, -0.12f, 7.75f) + vec3(0, -sinf(a), cosf(a)); dir[s] = vec3(0, -sinf(a), cosf(a)); strength[s] = 1.f; }
  return 2;
}

void Game::spawn(vec3 p, vec3 v, float life, float size, float grow, vec3 col, float alpha, int kind, float drag, float buoy) {
  if (particles.size() > 9000) return;
  particles.push_back({p, v, life, life, size, grow, col, alpha, kind, drag, buoy, false, true});
}

void Game::jetEffects(float dt) {
  const float thr = plane.ctl.throttle;
  float ab = plane.engineRunning ? smoothstepf(0.85f, 1.f, plane.engineSpool) : 0.f;
  bool wr = plane.spec->special == 2;
  vec3 exP[4], exD[4]; float exS[4];
  int nEx = jetExhausts(plane, exP, exD, exS);
  auto frand = [] { return (rand() % 1000) * 0.001f; };
  // The plume itself is ray-marched in the shader; particles add the hot debris it sheds: blue plasma sparks when
  // dry, a storm of amber embers in reheat (violet on the XR-40). Spread over the frame's flight path so they stream.
  for (int s = 0; s < nEx; s++) {
    vec3 exDir = plane.q.rotate(exD[s]);
    vec3 r = normalize(cross(exDir, plane.up()) + plane.right() * 1e-3f), u = cross(r, exDir);
    vec3 ex = plane.pos + plane.q.rotate(exP[s]);
    float rate = wr ? 0.f : 25.f * ab;    // embers per second per nozzle: reheat only (dry jets and the XR-40's plasma shed none)
    int n = (int)(rate * dt + frand());
    for (int i = 0; i < n; i++) {
      float k = frand(), hot = frand();
      vec3 jitter = r * ((frand() - 0.5f) * 0.55f) + u * ((frand() - 0.5f) * 0.35f);
      vec3 v = plane.vel + exDir * (90.f + 160.f * ab) * (0.6f + 0.6f * frand()) + jitter * (25.f + 40.f * ab);
      vec3 col = lerp(wr ? vec3(0.6f, 0.3f, 1.f) : vec3(1.f, 0.38f, 0.08f), wr ? vec3(0.8f, 0.9f, 1.f) : vec3(1.f, 0.8f, 0.45f), hot * hot) * (1.5f + 1.5f * hot);
      spawn(ex + jitter - plane.vel * (dt * k) + exDir * (2.f + k * 4.f), v, 0.15f + 0.3f * frand(), 0.035f + 0.03f * hot, -0.02f,
            col, 1.f, SPR_SPARK, 2.5f, 0.f);
    }
    // reheat: a faint heat haze trailing the plume at low level
    if (ab > 0.05f) {
      if (plane.agl() < 800.f && frand() < 30.f * dt)
        spawn(ex + exDir * (8.f + 6.f * ab), plane.vel * 0.2f + exDir * 20.f, 1.2f, 1.6f, 3.5f, vec3(0.32f, 0.3f, 0.3f), 0.06f * ab, SPR_SMOKE, 1.5f, 0.2f);
    }
  }
  // reheat light-off: a shock ring and a burst of sparks out of both nozzles
  if (ab > 0.08f && prevAB <= 0.08f) {
    for (int s = 0; s < nEx; s++) {
      vec3 exDir = plane.q.rotate(exD[s]);
      vec3 r = normalize(cross(exDir, plane.up()) + plane.right() * 1e-3f), u = cross(r, exDir);
      vec3 ex = plane.pos + plane.q.rotate(exP[s]) + exDir * 0.2f;
      spawn(ex, plane.vel + exDir * 30.f, 0.35f, 0.6f, 9.f, vec3(1.f, 0.7f, 0.4f) * 1.5f, 1.f, SPR_SHOCK, 0.f, 0.f);
      for (int i = 0; i < (wr ? 0 : 30); i++) {   // the XR-40's plasma lights with the shock ring alone
        vec3 j = r * (frand() - 0.5f) + u * (frand() - 0.5f);
        spawn(ex, plane.vel + exDir * (60.f + 120.f * frand()) + j * 70.f, 0.25f + 0.3f * frand(), 0.12f, -0.2f, vec3(1.f, 0.75f, 0.4f) * 4.f, 1.f, SPR_SPARK, 2.f, 0.f);
      }
    }
    for (int e = 0; e < nEx; ++e) g_audio.triggerLocal(SFX_REHEAT, exP[e], .45f / sqrtf(float(nEx)));
  }
  prevAB = ab;
  // hover downwash: dust or spray blown out in a ring under the jet
  float agl = plane.agl();
  if (plane.nozzle > 0.5f && thr > 0.25f && agl < 35.f) {
    float gy = std::max(g_world.height(plane.pos.x, plane.pos.z), 0.f);
    bool water = g_world.height(plane.pos.x, plane.pos.z) < 0.3f;
    int n = (int)((1.f - agl / 35.f) * 6.f);
    for (int i = 0; i < n; i++) {
      float ang = (rand() % 628) * 0.01f;
      vec3 d(cosf(ang), 0, sinf(ang));
      spawn(vec3(plane.pos.x, gy + 0.5f, plane.pos.z) + d * 3.f, d * (12.f + (rand() % 100) * 0.1f) + vec3(0, 1.5f, 0), 1.6f, 1.2f, 3.f,
            water ? vec3(0.9f, 0.95f, 1.f) : vec3(0.5f, 0.46f, 0.38f), water ? 0.5f : 0.35f, SPR_SMOKE, 1.2f, 0.3f);
    }
    if (wr && !water)   // the XR-40's plasma where each pod's jet strikes the ground: a violet glow, and sparks off it
      for (int s = 0; s < nEx; s++) {
        const vec3 ex = plane.pos + plane.q.rotate(exP[s]), d = plane.q.rotate(exD[s]);
        if (d.y > -0.5f) continue;
        const float t = (ex.y - gy) / -d.y;
        if (t > 30.f) continue;
        const float k = (1.f - t / 30.f) * smoothstepf(0.25f, 0.8f, thr);
        const vec3 hit = vec3(ex.x + d.x * t, gy + 0.25f, ex.z + d.z * t);
        spawn(hit, vec3(0.f), 0.05f, 1.6f + 2.4f * k, 0.f, vec3(0.6f, 0.42f, 1.f) * (1.5f * k), 1.f, SPR_GLOW, 0.f, 0.f);
        for (int i = 0; i < (int)(40.f * k * dt + frand()); i++) {
          const float a = frand() * 6.2832f;
          spawn(hit, vec3(cosf(a), 0.f, sinf(a)) * (8.f + 14.f * frand()) + vec3(0.f, 2.f + 4.f * frand(), 0.f), 0.25f + 0.3f * frand(), 0.03f, -0.02f,
                vec3(0.75f, 0.6f, 1.f) * 3.f, 1.f, SPR_SPARK, 1.5f, -6.f);
        }
      }
  }
  // The pilot feels transonic buffeting; an external observer hears a boom only when the Mach cone arrives.
  float M = plane.mach;
  if (M < .94f) audioMachArmed = true;
  if (audioMachArmed && M >= 1.f && !plane.onGround) {
    audioMachArmed = false;
    g_audio.triggerLocal(SFX_TRANSONIC, kModels[std::max(0, typeOf(plane.spec))].eye+vec3(0,0,-1), .45f);
    toast("MACH 1 - SUPERSONIC", vec3(0.4f, 0.9f, 1));
    vec3 f = normalize(plane.vel), r = normalize(cross(f, vec3(0, 1, 0)) + vec3(1e-4f, 0, 0)), u = cross(r, f);
    // the boom: two shock rings racing out, and a collar of condensation flashing round the airframe at the shock -
    // denser in damp air - for a fraction of a second, thinning as it falls back (a thin ring: thicker or longer-lived,
    // it swallowed the chase camera, the whole view white)
    bursts.push_back({plane.pos, r * 25.f, u * 25.f, vec3(0.7f, 0.85f, 1.f), 0.f, false});
    bursts.push_back({plane.pos - f * 6.f, r * 45.f, u * 45.f, vec3(0.55f, 0.7f, 1.f), 0.f, false});
    const float humid = clampf(0.35f + 0.45f * wx.cloudCover + (wx.precip ? 0.3f : 0.f), 0.f, 1.f) * smoothstepf(13000.f, 1500.f, plane.pos.y);
    const float span = plane.spec->span, len = plane.spec->fusLen, R0 = 0.3f * span + 1.2f;
    const int n = (int)(36.f + 54.f * humid);
    for (int i = 0; i < n; i++) {
      const float a = (i + frand()) * 6.2832f / n, along = (frand() - 0.35f) * len * 0.3f;
      const vec3 radial = r * cosf(a) + u * sinf(a);
      const float rr = R0 * (0.7f + 0.4f * frand());
      spawn(plane.pos + radial * rr - f * along, plane.vel + radial * (3.f + 4.f * frand()),
            0.3f + 0.3f * frand(), 0.5f + 0.5f * frand(), 1.5f + 1.5f * frand(), vec3(0.96f, 0.97f, 1.f), 0.22f + 0.2f * humid, SPR_SMOKE, 0.6f, 0.f);
    }
  }
  prevMach = M;
}

// ------------------------------------------------------------------ lights
// Every light but the sun and moon is a point (or spot) light with the aircraft's shadows, and sits in
// a modelled fixture: a faired housing with a domed lens (nav lights in the wingtips and tail cone, the strobes
// flashing through the wingtip lenses, a beacon on top, landing lights in the wing leading edges). The exhaust has a
// light in each flame just behind its nozzle, so the nozzles and the airframe throw shadows from it.
void Game::buildLights(FrameParams& fp) {
  fp.plN = 0; fp.plane.lensN = 0;
  auto light = [&](vec3 pos, float rad, vec3 col, float cosCut, vec3 dir, float shadow) {
    if (fp.plN < 12 && col.x + col.y + col.z > 1e-4f) fp.pl[fp.plN++] = {pos, rad, col, cosCut, dir, shadow};
  };
  if (length(fp.flameLight) > 0.f) light(fp.flameLightPos, 4.f, fp.flameLight, -2.f, vec3(0, 1, 0), 0.f);   // plasma blast
  if ((screen != SCR_FLIGHT && screen != SCR_LOADING) || !plane.spec || crashed) return;
  const AircraftSpec& s = *plane.spec;
  const ModelDef& md = kModels[typeOf(plane.spec)];
  float t = realTime, night = fp.night;
  float dark = s.special == 2 ? 1.f - somnus.stealth : 1.f;   // a cloaked XR-40 runs dark
  auto W = [&](vec3 b) { return plane.pos + plane.q.rotate(b); };
  auto lens = [&](vec3 b, vec3 axis, int tint, vec3 emit) {
    int i = fp.plane.lensN; if (i >= 6) return;
    float* P = fp.plane.lensP[i]; float* C = fp.plane.lensC[i]; float* D = fp.plane.lensD[i];
    P[0] = b.x; P[1] = b.y; P[2] = b.z; P[3] = 0; C[0] = emit.x; C[1] = emit.y; C[2] = emit.z; C[3] = 0;
    D[0] = axis.x; D[1] = axis.y; D[2] = axis.z; D[3] = (float)tint; fp.plane.lensN++;
  };
  // Exactly one light per live exhaust, with the same moving anchor/radius/power as its plume.
  // This replaces the old special-craft and separate Reverie paths; a failed or unfueled engine emits neither.
  if (!(camMode == 1 && fp.sealedCockpit)) {
    const ExhaustVisual& ex = fp.plane.exhaust;
    const float flick = .85f + .15f*sinf(t*57.f)*sinf(t*23.f + 1.f);
    for (int k = 0; k < ex.count; ++k) {
      const float sp = ex.power[k][0], ab = ex.power[k][1], intensity = ex.power[k][2];
      vec3 c = lerp(vec3(.3f, .55f, 1.f), vec3(1.f, .62f, .3f), ab)
               *((14.f*sp + 45.f*ab)*intensity*flick*dark/(float)std::max(s.engines, 1));
      light(W(exhaustPosition(ex, k) + exhaustDirection(ex, k)*(.15f + .15f*ab)),
            std::max(.05f, ex.exit[k][3]*.2f), c, -2.f, vec3(0, 1, 0), .05f);
    }
  }
  // fixture positions (body space)
  const bool xr50 = s.special == 3;   // (its tips swing with its wing)
  vec3 tip = xr50 ? morpheusWingTip(plane.sweep) : s.special == 2 ? kSomnusWingTip : s.special ? kJetWingTip : modelWingTip(md);
  vec3 tail = xr50 ? kMorpheusTail : s.special == 2 ? vec3(0, -0.1f, 7.86f) : s.special ? vec3(0, 0.45f, 7.6f) : modelTailTip(md);
  vec3 bcn = xr50 ? kMorpheusBeacon : s.special == 2 ? vec3(0, 0.53f, 1.6f) : s.special ? vec3(0, 0.67f, 1.6f) : modelFinTop(md);
  vec3 ldg;   // left landing light, in the wing leading edge
  if (xr50) ldg = kMorpheusLanding;
  else if (s.special == 2) ldg = vec3(-0.2f, -0.38f, -6.6f);   // XR-40: under the chin, ahead of the pods, turrets and gear
  else if (s.special) ldg = vec3(-1.8f, -0.26f, 0.15f);
  else { float k = 0.3f, x = md.wing[0] * k; ldg = vec3(-x, md.wing[4] + x * tanf(md.wing[6] * DEG), md.wing[5] + md.wing[3] * k - 0.02f); }
  bool on = plane.engineRunning || plane.onGround;
  float navL = on ? 0.06f * dark : 0.f;   // ~40 cd: a faint coloured wash on the wingtips, the ground when low
  float st = fmodf(t, 1.3f);
  bool strobe = on && !plane.onGround && dark > 0.5f && (st < 0.05f || (st > 0.12f && st < 0.16f));
  bool beacon = plane.engineRunning && dark > 0.5f && fmodf(t, 1.0f) < 0.12f;
  vec3 red(1.f, 0.08f, 0.04f), green(0.1f, 1.f, 0.25f), white(1.f, 0.97f, 0.9f);
  float lensK = 3.f + 40.f * night;   // a small, intense lamp: the bloom gives it its halo
  tip.x += 0.05f;   // the lens domes stand proud of the wingtips
  vec3 tl(-tip.x, tip.y, tip.z);
  vec3 sW = strobe ? white * 2.5f : vec3(0.f);
  lens(tl, vec3(-1, 0, 0), 0, red * (on ? dark : 0.f) * lensK + sW * 8.f);
  lens(tip, vec3(1, 0, 0), 1, green * (on ? dark : 0.f) * lensK + sW * 8.f);
  lens(tail, vec3(0, 0, 1), 2, white * (on ? dark : 0.f) * lensK);
  lens(bcn, vec3(0, 1, 0), 0, beacon ? red * 30.f : vec3(0.f));
  float land = fp.landLight;
  vec3 ldgR(-ldg.x, ldg.y, ldg.z);
  lens(ldg, vec3(0, 0, -1), 2, white * land * 25.f);
  lens(ldgR, vec3(0, 0, -1), 2, white * land * 25.f);
  light(W(tl + vec3(-0.07f, 0, 0)), 0.25f, red * navL + sW, -2.f, vec3(0, 1, 0), 0.03f);
  light(W(tip + vec3(0.07f, 0, 0)), 0.25f, green * navL + sW, -2.f, vec3(0, 1, 0), 0.03f);
  light(W(tail + vec3(0, 0, 0.07f)), 0.25f, white * navL, -2.f, vec3(0, 1, 0), 0.03f);
  if (beacon) light(W(bcn + vec3(0, 0.07f, 0)), 0.25f, red * 0.8f, -2.f, vec3(0, 1, 0), 0.03f);
  if (land > 0.f) {
    vec3 ld = normalize(plane.forward() - plane.up() * 0.1f);
    light(W(ldg + vec3(0, 0, -0.08f)), 0.3f, vec3(1.f, 0.95f, 0.85f) * land * 4500.f, 0.93f, ld, 0.03f);
    light(W(ldgR + vec3(0, 0, -0.08f)), 0.3f, vec3(1.f, 0.95f, 0.85f) * land * 4500.f, 0.93f, ld, 0.03f);
  }
}

// ------------------------------------------------------------------ UFO encounter
void Game::startUfo() {
  ufo = Ufo(); ufo.on = true; ufo.side = (rand() & 1) ? 1.f : -1.f;
  g_audio.triggerAt(SFX_UFO_ARRIVE, ufo.pos, .9f, plane.vel, 900001);
  toast("UNIDENTIFIED CONTACT", vec3(0.4f, 1.f, 0.6f));
}

void Game::updateUfo(float dt) {
  if (!ufo.on) {
    // summon: hold J and K together for a second while flying
    bool flying = screen == SCR_FLIGHT && !paused && !crashed && !plane.onGround;
    if (flying && in.down['J'] && in.down['K']) {
      ufoSummon += dt;
      if (ufoSummon >= 1.f) { ufoSummon = 0; startUfo(); return; }
    } else ufoSummon = 0;
    // random encounters: only while properly airborne
    bool ok = screen == SCR_FLIGHT && !crashed && !plane.onGround && plane.agl() > 150.f && length(plane.vel) > 25.f;
    if (ok) ufo.next -= dt;
    if (ufo.next <= 0 && ok) startUfo();
    return;
  }
  float t = (ufo.t += dt);
  // abort (zoom off) if the player crashes or lands
  if ((crashed || plane.onGround) && t < 23.f) { ufo.t = t = 23.f; }
  // frame that rides with the player: horizontal flight direction, its right, world up
  vec3 f = plane.vel; f.y = 0;
  f = length(f) > 1.f ? normalize(f) : normalize(vec3(plane.forward().x, 0, plane.forward().z) + vec3(1e-4f, 0, 0));
  vec3 r(-f.z, 0, f.x), u(0, 1, 0);
  float span = plane.spec->span;
  vec3 hold = r * (ufo.side * (span * 0.5f + 15.f)) + u * -0.6f + f * 2.f;         // station alongside, cabin at eye level
  vec3 from = -f * 250.f + u * 60.f + r * (ufo.side * 520.f);                       // swoops in from the rear quarter and above (in view of the XR-40's aft displays)
  float k = smoothstepf(0.f, 6.5f, t);
  vec3 off = from + (hold - from) * k;
  off.y += sinf(t * 1.7f) * 0.6f + sinf(t * 0.9f) * 0.4f;                           // floating bob
  if (t > 23.f) { float z = t - 23.f; off += f * (z * z * z * 120.f + z * 30.f) + u * (z * z * 45.f); }   // zoom away
  ufo.pos = plane.pos + off;
  // orientation: hatch side (+x) towards the player, tilted into the swoop
  vec3 toP = plane.pos - ufo.pos; toP.y = 0; toP = length(toP) > 1.f ? normalize(toP) : r * -ufo.side;
  float tilt = (1.f - k) * 0.35f * ufo.side + (t > 23.f ? -0.25f * ufo.side : 0.f) + 0.04f * sinf(t * 1.3f);
  vec3 x = toP, z = normalize(cross(x, u));
  vec3 xr = x * cosf(tilt) + u * sinf(tilt), yr = u * cosf(tilt) - x * sinf(tilt);
  ufo.right = xr; ufo.up = yr; ufo.fwd = z;
  // the show: hatch opens 8-10 s, dance 10-16, laugh 16-18.5, wave 18.5-21, hatch closes 21-23, zoom 23-26
  ufo.hatch = smoothstepf(8.f, 10.f, t) * (1.f - smoothstepf(21.f, 23.f, t));
  ufo.laugh = smoothstepf(15.8f, 16.3f, t) * (1.f - smoothstepf(18.3f, 18.8f, t));
  ufo.wave = smoothstepf(18.4f, 18.9f, t) * (1.f - smoothstepf(20.8f, 21.3f, t));
  if (t > 16.f && !ufo.sfxLaugh) { ufo.sfxLaugh = true; g_audio.triggerAt(SFX_UFO_LAUGH, ufo.pos, 1.f, plane.vel, 900001); }
  if (t > 23.f && !ufo.sfxZoom) {
    ufo.sfxZoom = true; g_audio.triggerAt(SFX_UFO_ZOOM, ufo.pos, 1.f, plane.vel, 900001);
    vec3 c = ufo.pos;
    bursts.push_back({c, ufo.right * 14.f, ufo.fwd * 14.f, vec3(0.4f, 1.f, 0.7f), 0.f});
    for (int i = 0; i < 40; i++) spawn(c, plane.vel + vec3((rand() % 200 - 100) * 0.3f, (rand() % 200 - 100) * 0.3f, (rand() % 200 - 100) * 0.3f), 0.6f, 0.25f, -0.3f, vec3(0.5f, 1.f, 0.8f) * 3.f, 1.f, SPR_SPARK, 1.f, 0.f);
  }
  if (t > 23.f && t < 26.f) spawn(ufo.pos, plane.vel * 0.5f, 0.5f, 3.f, -4.f, vec3(0.4f, 1.f, 0.8f) * 1.5f, 1.f, SPR_GLOW, 0.f, 0.f);   // light streak
  if (t > 26.5f) { ufo.on = false; ufo.next = 480.f + (rand() % 1000) * 0.42f; }   // next one in 8-15 minutes
}

// ------------------------------------------------------------------ lightning
// A stroke's channel: the line from the cloud to the ground broken again and again - each piece's middle pushed aside
// up to a fifth of its length, square to it - so it zigzags at every scale as a real one does; branches forking off its
// upper two thirds, down and out, some forking once more, none reaching the ground. Its light: the stepped leader
// feeling its way down for 45 ms (faint, only as far as it has come), then the return stroke, all of it blinding at
// once, then a restroke or three down the main channel 50 to 120 ms apart
float Game::Strike::flash(float at) const {
  float f = 0.f;
  for (int i = 0; i < strokes; i++) { const float d = at - restrike[i]; if (d >= 0.f) f = std::max(f, (i ? 0.8f : 1.f) * expf(-d * 22.f)); }
  return f;
}
// ------------------------------------------------------------------ crash wreckage
// the ground under the wreckage: the pits dug this flight in it (g_groundPits)
float Game::wreckGround(float x, float z) const { return pitGround(x, z, g_world.height(x, z, 6)); }
// this flight's pits, in the order the renderer draws them (frame(): the first 24): the crash crater, the pieces' pits,
// the plasma craters and the laser scorches, newest first
void Game::refreshGroundPits() {
  g_groundPits.clear(); g_scenery.craters.clear();
  if (screen != SCR_FLIGHT && screen != SCR_DEBRIEF) return;
  // craters flatten the scenery that stood in them (a plasma blast clears a far wider circle than its pit; laser scorch
  // pits don't: what a bolt destroys is tracked one object at a time)
  if (craterR > 0) g_scenery.craters.push_back(vec3(craterX, craterZ, craterR * 1.5f));
  for (const Pit& p : pits) if (p.R > 1.2f) g_scenery.craters.push_back(vec3(p.x, p.z, p.R * 1.3f));
  for (const vec3& f : somnus.flattened) g_scenery.craters.push_back(f);
  if (craterR > 0) g_groundPits.push_back({craterX, craterZ, craterR, craterD});
  for (const Pit& p : pits) g_groundPits.push_back({p.x, p.z, p.R, p.D});
  for (const auto& c : somnus.craters) g_groundPits.push_back({c.x, c.z, c.R, c.D});
  for (int i = (int)somnus.scorch.size() - 1; i >= 0; i--) g_groundPits.push_back({somnus.scorch[i].x, somnus.scorch[i].z, somnus.scorch[i].R, somnus.scorch[i].D});
  if (g_groundPits.size() > 24) g_groundPits.resize(24);   // (the renderer's 24)
}

// Splits the airframe into nose, centre section, both wings and tail (each the aircraft's field clipped to a
// body-space box), throws them apart with the impact energy, scatters skin fragments and digs a crater.
// A layered explosion: a white-hot flash that lights the scene, a fireball of flame cooling from yellow to deep red
// as it billows out and rises, a dark smoke column that keeps climbing and spreading after the flames die, arcing
// sparks, burning fragments that trail fire and smoke, and a shock ring. R: fireball radius (m).
void Game::fireball(vec3 c, vec3 baseV, float R, bool air, bool water) {
  Rng r((uint32_t)(c.x * 13.f + c.z * 7.f + realTime * 1000.f));
  auto sph = [&]() { vec3 d(r.range(-1, 1), r.range(-1, 1), r.range(-1, 1)); float l = length(d); return l > 1e-3f ? d / l : vec3(0, 1, 0); };
  boomT = 0; boomP = c + vec3(0, R * 0.4f, 0); boomI = R * R * 500.f;
  for (int i = 0; i < 3; i++) spawn(c + sph() * R * 0.2f, baseV, r.range(0.12f, 0.22f), R * r.range(0.9f, 1.3f), R * 1.5f, vec3(1.f, 0.9f, 0.7f) * 1.6f, 1.f, SPR_GLOW, 0.f, 0.f);   // flash
  if (water) {   // into the sea: a tall column of spray and steam instead of a fireball
    for (int i = 0; i < 110; i++) { vec3 d = sph(); d.y = fabsf(d.y) * 2.5f + 0.6f;
      spawn(c + vec3(0, 0.5f, 0), baseV * 0.3f + d * R * r.range(1.5f, 4.f), r.range(1.8f, 3.2f), R * r.range(0.15f, 0.35f), R * 0.5f, vec3(0.92f, 0.95f, 1.f), 0.8f, SPR_SMOKE, 1.f, -7.f); }
    bursts.push_back({c + vec3(0, 0.3f, 0), vec3(R * 3.f, 0, 0), vec3(0, 0, R * 3.f), vec3(0.8f, 0.9f, 1.f), 0.f});
    return;
  }
  int nf = (int)clampf(R * 9.f, 30.f, 90.f);
  for (int i = 0; i < nf; i++) {   // the fireball: billows of flame thrown out, rising, cooling to soot
    vec3 d = sph(); if (!air) d.y = fabsf(d.y) * 0.8f + 0.15f;
    float k = r.range(0.f, 1.f);
    vec3 col = lerp(vec3(1.f, 0.82f, 0.45f), vec3(1.f, 0.5f, 0.14f), k);
    spawn(c + d * R * 0.3f * k, baseV * 0.6f + d * R * r.range(0.9f, 2.2f), r.range(1.1f, 2.3f), R * r.range(0.3f, 0.55f), R * r.range(0.35f, 0.75f), col, 0.9f, SPR_FLAME, 2.2f, r.range(3.f, 6.f));
  }
  for (int i = 0; i < 8; i++)   // a little additive heat glow in the core
    spawn(c + sph() * R * 0.2f, baseV * 0.6f, r.range(0.3f, 0.5f), R * 0.6f, R * 0.5f, vec3(1.f, 0.6f, 0.25f), 0.2f, SPR_FIRE, 2.f, 1.f);
  int ns = (int)clampf(R * 6.f, 20.f, 60.f);
  for (int i = 0; i < ns; i++) {   // smoke: a column that keeps climbing long after the flames
    vec3 d = sph(); if (!air) { d.y = fabsf(d.y) + 0.6f; d.x *= 0.5f; d.z *= 0.5f; }
    float g = r.range(0.05f, 0.13f);
    spawn(c + d * R * 0.3f, baseV * 0.3f + d * R * r.range(0.3f, 0.8f) + plane.windVel * 0.5f, r.range(5.f, 10.f), R * r.range(0.3f, 0.5f), R * r.range(0.15f, 0.3f), vec3(g, g * 0.95f, g * 0.9f), r.range(0.4f, 0.6f), SPR_SMOKE, 0.5f, air ? 1.f : r.range(4.f, 7.f));
  }
  for (int i = 0; i < (int)clampf(R * 25.f, 50.f, 160.f); i++)   // sparks arcing out and falling
    spawn(c, baseV * 0.7f + sph() * r.range(15.f, 55.f) + vec3(0, r.range(0.f, 15.f), 0), r.range(0.8f, 2.4f), r.range(0.05f, 0.12f), -0.02f, vec3(1.f, 0.7f, 0.3f) * r.range(2.f, 5.f), 1.f, SPR_SPARK, 0.6f, -9.f);
  for (int i = 0; i < (int)clampf(R * 2.5f, 6.f, 18.f); i++) {   // burning fragments trailing fire and smoke
    Debris d;
    d.p = c + sph() * R * 0.3f;
    vec3 dir = sph(); if (!air) dir.y = fabsf(dir.y) + 0.4f;
    d.v = baseV * 0.7f + normalize(dir) * r.range(15.f, 40.f);
    d.w = sph() * r.range(4.f, 14.f); d.q = quat::axisAngle(sph(), r.range(0, 6.f));
    d.size = r.range(0.15f, 0.45f); d.charred = true; d.rest = false; d.burn = r.range(1.2f, 3.5f);
    d.body = debrisPanel(1.2f * d.size, 2.f * d.size, r.range(15.f, 30.f));
    debris.push_back(d);
  }
  vec3 f = air && length(baseV) > 1.f ? normalize(baseV) : vec3(0, 1, 0);
  vec3 rr = normalize(cross(f, fabsf(f.y) > 0.9f ? vec3(1, 0, 0) : vec3(0, 1, 0))), u = cross(rr, f);
  bursts.push_back({c, rr * R * 3.5f, u * R * 3.5f, vec3(1.f, 0.65f, 0.35f), 0.f});   // shock ring
}

// A piece of the wreck hitting the ground or the sea with E joules: a crater and a burst of earth, dust and sparks - or
// a fireball, if it was burning - all of it sized by the blow. A crater's size goes as the cube root of the energy that
// dug it (an engine coming down at 90 m/s: a 5 m pit; a propeller at 40: a scrape); the earth flies out faster and
// further, and the dust rises higher, from a harder blow
float Game::partImpact(vec3 p, vec3 v, float E, float fire, bool water) {
  Rng r((uint32_t)(p.x * 31.f + p.z * 17.f + realTime * 977.f));
  const float s = cbrtf(std::max(E, 1.f)), R = clampf(0.055f * s, 0.4f, 18.f);
  auto sph = [&]() { vec3 d(r.range(-1, 1), r.range(-1, 1), r.range(-1, 1)); float l = length(d); return l > 1e-3f ? d / l : vec3(0, 1, 0); };
  vec3 hd(v.x, 0, v.z); hd = length(hd) > 1.f ? normalize(hd) : vec3();   // (the way it was going: the earth flies on that way)
  // Impact energy controls source level/timbre; distance is applied once by the mixer, with no loudness floor.
  if (E > 80.f) g_audio.triggerAt(water ? SFX_SPLASH : SFX_IMPACT, p, clampf(s / 130.f, .06f, .85f));
  if (!water && E > 2500.f) g_audio.triggerAt(SFX_METAL, p, clampf(s / 220.f, .08f, .45f));
  if (!water && fire > .5f && E > 15000.f) g_audio.triggerAt(SFX_CRASH, p, clampf(s / 270.f * fire, .08f, .6f));
  if (water) {   // a column of spray as tall as the blow, and a ring racing out over the water
    const int n = (int)clampf(R * 24.f, 10.f, 110.f);
    for (int i = 0; i < n; i++) {
      vec3 d = sph(); d.y = fabsf(d.y) * 2.8f + 0.5f;
      spawn(p + vec3(0, 0.3f, 0), d * (r.range(0.8f, 1.6f) * (3.f + 4.f * sqrtf(R))), r.range(1.6f, 3.f), clampf(R * r.range(0.25f, 0.45f), 0.4f, 6.f), R * 0.4f, vec3(0.92f, 0.95f, 1.f), 0.8f, SPR_SMOKE, 1.f, -7.f);
    }
    bursts.push_back({p + vec3(0, 0.2f, 0), vec3(R * 3.f, 0, 0), vec3(0, 0, R * 3.f), vec3(0.8f, 0.9f, 1.f), 0.f});
    return 0.f;
  }
  // the pit (a scrape below 0.6 m is earth thrown up, no more), the smallest given up for a bigger one past the limit
  float dug = 0.f;
  if (R >= 0.6f) {
    if ((int)pits.size() >= kMaxPits) {
      auto least = std::min_element(pits.begin(), pits.end(), [](const Pit& a, const Pit& b) { return a.R < b.R; });
      if (least->R < R) pits.erase(least);
    }
    if ((int)pits.size() < kMaxPits) { dug = R * r.range(0.26f, 0.34f); pits.push_back({p.x, p.z, R, dug}); refreshGroundPits(); }   // (the ground the pieces meet has it at once)
  }
  // earth thrown out of it: clods arcing out and falling back, downrange more than up
  const int nc = (int)clampf(8.f + R * 16.f, 8.f, 160.f);
  const float ve = 4.f + 7.f * sqrtf(R);
  for (int i = 0; i < nc; i++) {
    const float a = r.range(0.f, 6.2832f), el = r.range(0.45f, 1.3f), sp = ve * r.range(0.45f, 1.f);
    vec3 out = normalize(vec3(cosf(a), 0, sinf(a)) + hd * 0.7f);
    const float k = r.range(0.7f, 1.1f);
    spawn(p + vec3(0, 0.2f, 0), out * (sp * cosf(el)) + vec3(0, sp * sinf(el), 0), r.range(1.f, 2.2f), clampf(R * r.range(0.08f, 0.2f), 0.12f, 2.5f), 0.2f,
          vec3(0.3f, 0.24f, 0.17f) * k, 0.95f, SPR_SMOKE, 0.6f, -9.f);
  }
  // the dust: a skirt rolling out low over the ground, and a cloud rising over the pit that drifts off on the wind
  if (dev::env("WRECKDBG")) printf("impact at %.1f s: %.0f kJ, crater %.1f m%s\n", crashTimer, E * 1e-3f, R, fire > 0.3f ? ", burning" : "");
  const int nd = (int)clampf(6.f + R * 5.f, 6.f, 60.f);
  for (int i = 0; i < nd; i++) {
    const float a = r.range(0.f, 6.2832f);
    const bool skirt = i % 2 == 0;
    vec3 out = vec3(cosf(a), 0, sinf(a));
    vec3 dv = skirt ? out * r.range(2.f, 3.f + 2.5f * R) + vec3(0, r.range(0.2f, 1.f), 0) : out * r.range(0.5f, 1.5f + R) + vec3(0, r.range(1.f, 2.f + 1.2f * R), 0);
    spawn(p + vec3(0, 0.4f, 0), dv + plane.windVel * 0.3f, r.range(2.5f, 5.f) * (1.f + 0.06f * R), clampf(R * r.range(0.4f, 0.7f), 0.6f, 9.f), R * r.range(0.25f, 0.5f) + 0.4f,
          vec3(0.46f, 0.39f, 0.31f) * r.range(0.8f, 1.05f), skirt ? 0.35f : 0.4f, SPR_SMOKE, skirt ? 1.4f : 0.8f, skirt ? 0.2f : r.range(0.6f, 1.6f));
  }
  bursts.push_back({p + vec3(0, 0.3f, 0), vec3(R * 4.f, 0, 0), vec3(0, 0, R * 4.f), vec3(0.55f, 0.46f, 0.36f), 0.f});   // the shock, racing out over the ground
  // a burning piece: what fuel it had goes up with it; a cold one throws sparks off the metal
  if (fire > 0.3f) fireball(p + vec3(0, 0.5f, 0), v * 0.08f, clampf(0.6f * R * fire + 1.f, 1.5f, 10.f), false, false);
  else {
    const int ns = (int)clampf(R * 14.f, 6.f, 70.f);
    for (int i = 0; i < ns; i++)
      spawn(p + vec3(0, 0.2f, 0), hd * r.range(3.f, 12.f) + sph() * r.range(4.f, 6.f + 6.f * sqrtf(R)) + vec3(0, r.range(1.f, 6.f), 0), r.range(0.3f, 0.9f), r.range(0.04f, 0.09f), -0.02f,
            vec3(1.f, 0.7f, 0.3f) * r.range(2.f, 4.f), 1.f, SPR_SPARK, 0.6f, -9.f);
  }
  return dug;
}

void Game::breakUp(vec3 impactVel, bool water, bool air) {
  shake(1.f);
  // Capture the ordinary production pose before the caller clears velocity.
  // In particular, speed-dependent nose steering must not snap to its taxi angle.
  FrameParams captured{};
  fillPlaneVisual(captured.plane, plane, propAngle, false);
  somnusVisual(captured);
  wreckPlanePose = captured.plane;
  const AircraftSpec& s = *plane.spec;
  Rng r(1234 + (uint32_t)(flightClock * 100));
  float speed = length(impactVel);
  // the airframe comes apart into its own components (breakup.h): props, nacelles, gear legs, struts, the tail's halves,
  // the fins, the wings and the fuselage in three, each flying on what it is made of
  BreakPiece P[kMaxBreakPieces];
  const int n = breakPieces(s, plane.gear, plane.gearHeight(), P);
  DebrisBody B[kMaxBreakPieces];
  breakBodies(s, P, n, plane.mass(), plane.payload, B);
  wreck.clear();
  const vec3 centre = plane.pos + plane.q.rotate(s.special ? vec3() : aeroGeom(s).cg);   // (the airframe's centre of mass)
  float energy = clampf(speed / 50.f, 0.4f, 2.5f);
  airBreak = air;
  vec3 vRot = plane.q.rotate(plane.w);
  for (int i = 0; i < n; i++) {
    WreckPiece w;
    w.C = P[i].C; w.H = P[i].H; w.kind = P[i].kind; w.side = P[i].side; w.rigidOnly = P[i].rigidOnly; w.body = B[i];
    w.cutN = breakCuts(s, P, n, i, w.cut);
    w.q = plane.q;
    w.c = plane.pos + plane.q.rotate(B[i].cg);
    // what burns: the wings' tanks, the engines and the fuselage; the light pieces only smoulder
    const int k = P[i].kind;
    w.fire = k == BK_WING || k == BK_CENTRE ? r.range(0.7f, 1.f) : k == BK_NACELLE ? r.range(0.6f, 0.9f) : k == BK_NOSE ? r.range(0.35f, 0.65f) :
             k == BK_AFT ? r.range(0.2f, 0.4f) : k == BK_TAIL || k == BK_FIN || k == BK_CANARD ? r.range(0.05f, 0.15f) : 0.f;
    if (s.special) w.fire = std::max(w.fire, 0.6f);
    // (a small piece is flung harder, and spins faster)
    const float small = clampf(4.f / std::max(B[i].size, 0.5f), 0.6f, 2.f);
    vec3 ax = normalize(vec3(r.range(-1, 1), r.range(-1, 1), r.range(-1, 1)));
    if (air) {
      // in flight: every piece keeps the airframe's momentum and rotation, is flung apart by the failure and tumbles;
      // its own air takes it from there - nothing touches the ground (no crater) until the pieces fall out of the sky
      vec3 arm = w.c - centre, out = length(arm) > 0.1f ? normalize(arm) : vec3(0, 1, 0);
      w.v = plane.vel * r.range(0.96f, 1.f) + cross(vRot, arm) + out * (r.range(6.f, 14.f) * small) + vec3(r.range(-3, 3), r.range(-2, 4), r.range(-3, 3));
      w.w = vRot + ax * (r.range(1.5f, 4.f) * small);
    } else {
      vec3 out = w.c - centre; out.y = 0; out = length(out) > 0.1f ? normalize(out) : normalize(vec3(r.range(-1, 1), 0, r.range(-1, 1)));
      w.v = impactVel * r.range(0.25f, 0.45f) + out * (r.range(4.f, 9.f) * energy * small) + vec3(0, r.range(5.f, 11.f) * energy, 0);
      if (water) w.v = w.v * 0.4f;
      w.w = ax * (r.range(1.f, 4.f) * energy * small);
    }
    w.rest = false; w.landed = false;
    wreck.push_back(w);
  }
  // the pieces that burn hottest go off again over the next couple of seconds
  std::vector<int> hot;
  for (int i = 0; i < n; i++) if (wreck[i].fire > 0.5f) hot.push_back(i);
  if (hot.empty()) hot.push_back(n - 1);
  // torn skin and chunks of structure: plates that flutter down on their own air (a chunk of structure is heavier)
  debris.clear();
  auto fragment = [&](vec3 p, vec3 v, float size, bool charred, float sigma, float spin) {
    Debris d;
    d.p = p; d.v = v; d.size = size; d.charred = charred; d.rest = false;
    d.w = normalize(vec3(r.range(-1, 1), r.range(-1, 1), r.range(-1, 1))) * spin;
    d.q = quat::axisAngle(normalize(vec3(r.range(-1, 1), r.range(-1, 1), r.range(-1, 1))), r.range(0, 6.f));
    d.body = debrisPanel(1.2f * size, 2.f * size, sigma);   // (the plate drawn: 2 x 1.2 sizes - prims.glsl traceDebris)
    debris.push_back(d);
  };
  if (air) {
    for (int i = 0; i < 16; i++) {
      const bool ch = (i % 3) == 0;
      fragment(centre + vec3(r.range(-3, 3), r.range(-1.5f, 1.5f), r.range(-4, 4)), plane.vel * r.range(0.6f, 0.95f) + normalize(vec3(r.range(-1, 1), r.range(-1, 1), r.range(-1, 1))) * r.range(10.f, 35.f),
               r.range(0.25f, 0.8f), ch, ch ? r.range(18.f, 35.f) : r.range(8.f, 16.f), r.range(6.f, 18.f));
    }
    craterR = 0;
    crashEndT = 1e9f;   // set once everything is down
    // the failure: the fuel tanks flash into a fireball smeared along the flight path, the slipstream tears the
    // skin away, and over the next couple of seconds the pieces go off again one after another
    pops.clear();
    for (int i = 0; i < 4; i++) {
      float k = i / 3.f;
      fireball(centre - plane.vel * (0.06f * k), plane.vel * (0.85f - 0.25f * k), 4.f + 3.f * (1.f - k), true, false);
    }
    for (int i = 0; i < 24; i++) {   // condensation and fuel mist torn off at the break
      vec3 v(r.range(-25, 25), r.range(-20, 25), r.range(-25, 25));
      spawn(centre, plane.vel * r.range(0.5f, 0.9f) + v, r.range(0.6f, 1.4f), r.range(2.f, 4.f), 6.f, vec3(0.9f, 0.92f, 0.95f), 0.35f, SPR_SMOKE, 3.f, 0.f);
    }
    for (int i = 0; i < 6; i++) pops.push_back({vec3(), vec3(), r.range(0.35f, 2.6f), r.range(2.f, 4.5f), hot[i % hot.size()]});
    for (int i = 0; i < 20; i++) {   // extra torn skin panels
      const size_t at = debris.size();
      fragment(centre + vec3(r.range(-3, 3), r.range(-1.5f, 1.5f), r.range(-4, 4)), plane.vel * r.range(0.5f, 0.9f) + normalize(vec3(r.range(-1, 1), r.range(-1, 1), r.range(-1, 1))) * r.range(15.f, 45.f),
               r.range(0.2f, 0.7f), (i % 2) == 0, r.range(4.f, 8.f), r.range(8.f, 22.f));
      debris[at].burn = i % 3 == 0 ? r.range(1.f, 3.f) : 0.f;
    }
    return;
  }
  for (int i = 0; i < 16; i++) {
    float a = r.range(0, 6.2832f);
    vec3 v = vec3(cosf(a), 0, sinf(a)) * r.range(6.f, 22.f) * energy + vec3(0, r.range(6.f, 18.f) * energy, 0) + impactVel * r.range(0.2f, 0.5f);
    if (water) v = v * 0.5f;
    const bool ch = (i % 3) == 0;
    fragment(centre + vec3(r.range(-2, 2), r.range(0.5f, 2.f), r.range(-2, 2)), v, r.range(0.25f, 0.8f), ch, ch ? r.range(18.f, 35.f) : r.range(8.f, 16.f), r.range(3.f, 12.f));
  }
  // crater (on land only): as the cube root of the blow - the aircraft's mass and its speed into the ground (a light
  // aircraft at 70 m/s: some 7 m across; an airliner or a research jet far faster: up to 20 m)
  const float vn = std::max(-impactVel.y, 0.f), Eh = 0.5f * plane.mass() * (vn * vn + 0.3f * std::max(speed * speed - vn * vn, 0.f));
  const float sE = cbrtf(std::max(Eh, 1.f));
  if (!water) {
    craterX = centre.x; craterZ = centre.z;
    craterR = clampf(0.06f * sE, 3.f, 20.f);
    craterD = craterR * 0.3f;
  } else craterR = 0;
  // the impact: a fireball (or a column of spray) as big as the fuel and the blow, the earth thrown up, and a couple
  // of secondary blasts as fuel goes up
  pops.clear();
  float R = clampf(2.f + 0.035f * sE + sqrtf(std::max(plane.fuel, 0.f)) * 0.12f, 4.f, 16.f);
  fireball(centre + vec3(0, 1.f, 0), impactVel * 0.15f, R, false, water);
  if (!water) {
    const int nd = (int)clampf(craterR * 8.f, 40.f, 180.f);
    for (int i = 0; i < nd; i++) spawn(centre, vec3(r.range(-12, 12), r.range(6, 20), r.range(-12, 12)) * (energy * 0.6f * clampf(craterR / 7.f, 0.8f, 2.2f)), r.range(1.5f, 3.f), r.range(1.2f, 2.6f) * clampf(craterR / 7.f, 0.8f, 2.f), 2.f, vec3(0.32f, 0.25f, 0.18f), 0.85f, SPR_SMOKE, 1.5f, -5.f);   // dirt
    bursts.push_back({centre + vec3(0, 0.3f, 0), vec3(craterR * 4.f, 0, 0), vec3(0, 0, craterR * 4.f), vec3(0.55f, 0.46f, 0.36f), 0.f});
    for (int i = 0; i < 3; i++) pops.push_back({vec3(), vec3(), r.range(0.4f, 2.2f), R * r.range(0.4f, 0.6f), hot[(size_t)r.range(0.f, hot.size() - 0.01f)]});
  }
}

void Game::updateWreck(float dt) {
  const float G = 9.81f;
  for (auto& tt : pieceTrail) {   // smoke trails drift with the wind and fade out
    for (auto& q : tt) { q.age += dt; q.p += (plane.windVel + vec3(0, 0.6f, 0)) * dt; }
    while (!tt.empty() && tt.front().age > 7.f) tt.erase(tt.begin());
  }
  if (boomT >= 0) boomT += dt;
  for (size_t i = 0; i < pops.size(); i++) {   // secondary explosions on the pieces
    pops[i].t -= dt;
    if (pops[i].t > 0) continue;
    const Pop pp = pops[i];
    pops.erase(pops.begin() + i); i--;
    if (pp.piece < (int)wreck.size()) {
      const WreckPiece& w = wreck[pp.piece];
      bool wet = g_world.height(w.c.x, w.c.z) < 0.5f && w.c.y < 1.f;
      if (!(wet && w.c.y < -1.f)) { fireball(w.c, w.v, pp.R, airBreak && !w.landed, wet); g_audio.triggerAt(wet ? SFX_SPLASH : SFX_CRASH, w.c, clampf(pp.R*.065f,.08f,.5f)); }
    }
  }
  for (WreckPiece& w : wreck) {
    // fire and smoke from the burning pieces (stronger right after the impact)
    float heat = w.fire * clampf(1.2f - crashTimer * 0.06f, 0.3f, 1.f);
    bool wet = g_world.height(w.c.x, w.c.z) < 0.5f;
    if (!wet && rand() % 100 < (int)(heat * 40)) {
      const vec3 he = (w.body.hi - w.body.lo) * 0.5f, ce = (w.body.hi + w.body.lo) * 0.5f - w.body.cg;   // (over what the piece is made of)
      vec3 jp = w.c + w.q.rotate(ce + vec3((rand() % 100 - 50) * 0.01f * he.x, 0, (rand() % 100 - 50) * 0.01f * he.z));
      spawn(jp, vec3(0, 2.f + (rand() % 100) * 0.02f, 0), 0.7f, 0.45f + 0.5f * heat, 0.8f, vec3(1.f, 0.42f, 0.1f) * 0.55f, 1.f, SPR_FIRE, 0.5f, 1.f);
    }
    bool sunk = wet && w.c.y < -1.5f, flown = false;
    if (!sunk && rand() % 100 < (int)(heat * 22)) spawn(w.c + vec3(0, 2.5f, 0), vec3((rand() % 100 - 50) * 0.02f, 3.5f, (rand() % 100 - 50) * 0.02f) + plane.windVel * 0.5f, 7.f, 1.5f, 2.5f, wet ? vec3(0.8f) : vec3(0.1f, 0.095f, 0.09f), 0.45f, SPR_SMOKE, 0.25f, 1.5f);
    if (w.rest) continue;
    if (wet && w.c.y < 0.3f) {
      if (!w.landed) {   // a piece falling into the sea: a splash as big as the blow
        w.landed = true;
        const float vn = std::max(-w.v.y, 0.f), E = 0.5f * w.body.mass * (vn * vn + 0.25f * std::max(dot(w.v, w.v) - vn * vn, 0.f));
        if (E > 1500.f && (airBreak || crashTimer > 0.3f)) partImpact(vec3(w.c.x, 0.2f, w.c.z), w.v, E, 0.f, true);
      }
      // in the sea: heavy drag, a short float on trapped air, then the piece sinks out of sight to the seabed
      float floatT = 1.f + 1.5f * w.fire;
      float vy = crashTimer < floatT ? (-0.35f - w.c.y) * 2.f : -2.2f - 1.2f * w.fire;
      w.v.x *= expf(-1.5f * dt); w.v.z *= expf(-1.5f * dt);
      w.v.y += (vy - w.v.y) * std::min(1.f, 3.f * dt);
      w.w = w.w * expf(-1.2f * dt);
      if (crashTimer > floatT && w.c.y > -12.f && rand() % 100 < 25)   // air escaping as it goes down
        spawn(vec3(w.c.x + (rand() % 100 - 50) * 0.03f, 0.05f, w.c.z + (rand() % 100 - 50) * 0.03f), vec3(0, 0.6f, 0), 0.9f, 0.35f, 1.5f, vec3(0.9f, 0.95f, 1.f), 0.45f, SPR_SMOKE, 1.f, 0.f);
    } else if (!w.landed) {
      // in the air, on its own: its surfaces' lift and drag as plates, its fuselage as bluff slices (breakup.h) - a wing
      // tumbles and drifts, an engine falls like a stone
      debrisStep(w.body, w.c, w.v, w.q, w.w, plane.windVel, isaDensity(std::max(w.c.y, 0.f)), dt);
      flown = true;
      // the smoke trail is a ribbon through one point per frame (continuous at any speed); the flame is drawn as a jet
      int k = (int)(&w - &wreck[0]);
      if (airBreak && k < kMaxBreakPieces && w.fire > 0.25f) {
        float heatT = clampf(1.3f - crashTimer * 0.08f, 0.35f, 1.f);
        pieceTrail[k].push_back({w.c, 0.f, (0.5f + 0.5f * w.fire) * heatT, 1});
        if (pieceTrail[k].size() > 600) pieceTrail[k].erase(pieceTrail[k].begin());
      }
    } else w.v.y -= G * dt;
    if (!flown) {
      w.v = w.v * expf(-0.08f * dt);
      w.c += w.v * dt;
      float wl = length(w.w);
      if (wl > 1e-4f) { w.q = quat::axisAngle(w.w, wl * dt) * w.q; w.q.normalize(); }
    }
    // ground contact against the corners of what the piece is made of
    float pen = 0; vec3 hitArm;
    const vec3 lo = w.body.lo - w.body.cg, hi = w.body.hi - w.body.cg;
    for (int k = 0; k < 8; k++) {
      vec3 cl((k & 1) ? hi.x : lo.x, (k & 2) ? hi.y : lo.y, (k & 4) ? hi.z : lo.z);
      vec3 cw = w.c + w.q.rotate(cl * 0.9f);
      float g = wreckGround(cw.x, cw.z);
      if (g - cw.y > pen) { pen = g - cw.y; hitArm = cw - w.c; }
    }
    if (pen > 0 && !w.landed && (airBreak || crashTimer > 0.3f)) {
      // first contact: the blow is the piece's own - its mass, and its speed into the ground with a little of its skid
      // (an impact's own pieces, thrown from the fireball, once they come down again). Where it digs a pit - under it -
      // or comes down in the crash's own crater, it drives itself into the ground there, a share of its own thickness
      // and of the pit's depth, and the earth takes its way: it stays where it struck, half buried
      w.landed = true;
      const float vn = std::max(-w.v.y, 0.f), E = 0.5f * w.body.mass * (vn * vn + 0.25f * std::max(dot(w.v, w.v) - vn * vn, 0.f));
      float dug = 0.f;
      if (E > 1500.f) dug = partImpact(vec3(w.c.x, wreckGround(w.c.x, w.c.z), w.c.z), w.v, E, w.fire, false);
      else if (craterR > 0.f && hypotf(w.c.x - craterX, w.c.z - craterZ) < craterR) dug = craterD;
      if (dug > 0.f) {
        const vec3 ext = hi - lo;
        w.bury = std::min(0.45f * std::min(ext.x, std::min(ext.y, ext.z)), 0.6f * dug);
        w.v.x *= 0.3f; w.v.z *= 0.3f;
      }
    }
    if (w.landed) w.groundT += dt;
    pen -= w.bury;   // (its crater's floor that far over its lowest point: it sinks in until it gets there)
    if (pen > 0) {
      // down on the ground: no rebound - the earth takes the blow - and it slides, rolls and turns to a stop, a piece
      // dug in far sooner; at rest once still and its pit is under it (dug the frame it struck, drawn the next)
      w.c.y += pen;
      if (w.v.y < 0) w.v.y = 0.f;
      const float grip = expf(-(w.bury > 0.f ? 14.f : 7.f) * dt);
      w.v.x *= grip; w.v.z *= grip;
      w.w = w.bury > 0.f ? w.w * expf(-12.f * dt) : w.w * expf(-4.f * dt) + cross(hitArm, vec3(w.v.x, 0, w.v.z)) * (1.2f * dt);
      if (w.groundT > 0.25f && length(w.v) < 0.8f && length(w.w) < 0.35f) w.rest = true;
    }
  }
  // in-flight break-up: the camera rides with the centre section; results come up a while after the last piece is down
  if (airBreak && !wreck.empty()) {
    plane.pos = wreck.back().c;   // (the centre section)
    bool all = true; for (auto& w : wreck) all = all && (w.landed || w.c.y < -1.f);
    if (crashEndT > 1e8f && (all || crashTimer > 150.f)) crashEndT = crashTimer + 6.f;
  }
  for (Debris& d : debris) {
    if (d.burn > 0 && !d.rest) {   // a burning fragment: a flame and a thin smoke trail behind it
      d.burn -= dt;
      float k = clampf(d.burn, 0.f, 1.f);
      spawn(d.p, d.v * 0.1f, 0.25f, 0.25f + 0.5f * d.size * k, 0.6f, vec3(1.f, 0.5f, 0.15f) * (0.6f + 0.6f * k), 1.f, SPR_FIRE, 2.f, 0.5f);
      if (rand() % 2) spawn(d.p, d.v * 0.05f, 1.6f, 0.3f + d.size * 0.5f, 1.4f, vec3(0.1f), 0.35f * k + 0.1f, SPR_SMOKE, 1.f, 0.8f);
    }
    if (d.rest) continue;
    float g = wreckGround(d.p.x, d.p.z);
    if (g >= 0.f || d.p.y >= 0.f) debrisStep(d.body, d.p, d.v, d.q, d.w, plane.windVel, isaDensity(std::max(d.p.y, 0.f)), dt);   // (a plate: it flutters and drifts down)
    else {   // (under the sea: sinking, below)
      d.v.y -= G * dt; d.p += d.v * dt;
      float wl = length(d.w);
      if (wl > 1e-4f) { d.q = quat::axisAngle(d.w, wl * dt) * d.q; d.q.normalize(); }
    }
    if (g < 0 && d.p.y < 0.f) {   // fragments in the sea flutter down to the seabed
      d.v = d.v * expf(-4.f * dt); d.v.y = std::max(d.v.y, -0.9f - d.size); d.w = d.w * expf(-1.f * dt);
      if (d.p.y < g + d.size * 0.15f) { d.p.y = g + d.size * 0.15f; d.rest = true; }
      continue;
    }
    if (d.p.y < g + d.size * 0.15f) {
      d.p.y = g + d.size * 0.15f;
      if (-d.v.y > 8.f) {   // a chunk coming down hard throws up a puff of earth (and, hard enough, digs a little pit)
        const float E = 0.5f * d.body.mass * d.v.y * d.v.y;
        const float dug = E > 4000.f ? partImpact(d.p, d.v, E, 0.f, false) : 0.f;
        if (dug > 0.f) {   // (one that dug its pit stays stuck in it, edge first, at the angle it struck, half buried)
          d.p.y = g - dug; d.v = vec3(); d.w = vec3(); d.rest = true;
          continue;
        }
      }
      d.v.y = 0.f; d.v.x *= 0.45f; d.v.z *= 0.45f; d.w = d.w * 0.5f;   // (no rebound: it skids and tumbles to a stop)
      if (length(d.v) < 0.7f) {  // settle flat on the ground
        d.rest = true;
        float yaw = atan2f(d.q.rotate(vec3(1, 0, 0)).z, d.q.rotate(vec3(1, 0, 0)).x);
        d.q = quat::axisAngle(vec3(0, 1, 0), -yaw) * quat::axisAngle(vec3(1, 0, 0), (d.size - 0.5f) * 0.4f);
      }
    }
  }
}

// ------------------------------------------------------------------ particles
void Game::updateParticles(float dt) {
  for (size_t i = 0; i < bursts.size();) { bursts[i].t += dt; if (bursts[i].t > 0.9f) { bursts[i] = bursts.back(); bursts.pop_back(); } else i++; }
  // the air the airframe moves (cloud wisps): near it, the flow round it - lifted ahead of the wing, pushed aside by the
  // nose, blown back and turned by a slipstream, pushed down behind the wing and round its tips (aero_wake.h); once
  // behind the wing, a parcel close to one of the pair's vortices is caught by it and turns round it as the pair sinks
  const AeroWake& wk = plane.wake;
  const bool flow = plane.spec && !plane.ev.crashed && wk.V > 3.f;
  const AeroGeom* ag = flow && plane.spec->special == 0 ? &aeroGeom(*plane.spec) : nullptr;
  const float reach = std::max(wk.span, 4.f);
  for (size_t i = 0; i < particles.size();) {
    Particle& p = particles[i];
    // spawned this frame: emitters already place particles along the path flown this frame, so moving them by a
    // whole frame of velocity too would carry them that far ahead (at 25 fps and Mach 0.9, past the nose)
    if (p.fresh) { p.fresh = false; i++; continue; }
    p.life -= dt;
    if (p.life <= 0) { particles[i] = particles.back(); particles.pop_back(); continue; }
    p.v = p.v * expf(-p.drag * dt);
    p.v.y += p.buoy * dt;
    if (p.air && flow) {
      if (p.caught) { p.sw.c += p.v * dt; p.p = aeroSwirlStep(p.sw, p.p, dt); }
      else {
        const vec3 rb = plane.q.conj().rotate(p.p - plane.pos);
        if (fabsf(rb.x) < 1.5f * reach && fabsf(rb.y) < reach && rb.z > -1.5f * reach && rb.z < wk.pairZ + reach)
          p.p += plane.q.rotate(aeroWakeInduced(wk, ag, rb)) * dt;
        if (!p.passed && rb.z > wk.pairZ + 0.3f * reach) { p.passed = true; p.caught = aeroWakeCatch(wk, plane.pos, plane.q, p.p, p.sw); }
      }
    }
    p.p += p.v * dt;
    p.size += p.grow * dt;
    i++;
  }
}
