// Somnium Volandi - the frame assembled for the renderer: FrameParams and the sprites
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

// ------------------------------------------------------------------ frame assembly
void fillPlaneVisual(PlaneVisual& pv, const Plane& p, float propAngle, bool inside) {
  const AircraftSpec& s = *p.spec;
  int idx = typeOf(p.spec);
  const ModelDef& md = kModels[idx];
  pv.on = true;
  pv.pos = p.pos;
  vec3 r = p.right(), u = p.up(), b = p.q.rotate(vec3(0, 0, 1));
  float m[9] = {r.x, r.y, r.z, u.x, u.y, u.z, b.x, b.y, b.z};
  memcpy(pv.rot, m, sizeof(m));
  packModel(s, idx, p.gearHeight(), pv.M);
  // nose/tail wheel steering: same angle the physics applies to the wheel (geometry angle in the x-z plane)
  float spd = length(p.vel);
  float steer = p.ctl.yaw * 0.45f * smoothstepf(30.f, 4.f, spd);
  if (s.taildragger) steer = -steer;
  pv.PS[0] = p.gear; pv.PS[1] = p.flaps; pv.PS[2] = steer; pv.PS[3] = inside ? 1.f : 0.f;
  // (the surfaces as the physics deflects them: the stick, the trim, the factory's rigging and any augmenter)
  if (s.special == 0 && p.flightTime > 0.f) { pv.Ctl[0] = p.ctlSurf.x; pv.Ctl[1] = p.ctlSurf.y; pv.Ctl[2] = p.ctlSurf.z; }   // (a display aircraft that never flew: its stick)
  else { pv.Ctl[0] = clampf(p.ctl.pitch + p.ctl.trim * 0.3f, -1, 1); pv.Ctl[1] = clampf(p.ctl.roll, -1, 1); pv.Ctl[2] = clampf(p.ctl.yaw, -1, 1); }
  pv.Ctl[3] = p.ctl.throttle;
  float blur = s.engineType == ENG_JET ? 1.f : smoothstepf(45.f, 650.f, p.rpm);
  float wr, nr; modelWheelRadii(idx, wr, nr);
  pv.model = typeOf(p.spec);
  pv.wheel[0] = p.wheelMotion[0].angle(wr); pv.wheel[1] = p.wheelMotion[1].angle(wr); pv.wheel[2] = p.wheelMotion[2].angle(nr);
  pv.Pr[0] = propAngle; pv.Pr[1] = blur; pv.Pr[2] = (float)std::max(s.blades, 2); pv.Pr[3] = p.flapLeft() - p.flaps;   // (a split flap: the left one's own)
  pv.I0[0] = p.ias * MS_TO_KT; pv.I0[1] = p.pos.y * M_TO_FT; pv.I0[2] = p.heading(); pv.I0[3] = p.vel.y * 196.85f;
  pv.I1[0] = p.pitchDeg(); pv.I1[1] = p.bankDeg();
  pv.I1[2] = s.engineType == ENG_PISTON ? p.rpm / std::max(s.maxRpm, 1.f) : p.n1 / 100.f; pv.I1[3] = p.fuel / std::max(s.maxFuel, 1.f);
  for (int i=0;i<4;i++) pv.engineHealth[i] = i<s.engines ? p.fail.engineHealth[i] : 0.f; // truthful per-engine cockpit failure indication
  pv.I2[0] = -p.q.rotate(p.w).y / DEG; pv.I2[1] = p.beta / DEG; pv.I2[2] = p.flaps; pv.I2[3] = p.gear;
  pv.colBase = s.colBase; pv.colStripe = s.colStripe;
  { std::string r = registrationOf(s); for (int i = 0; i < 3; i++) pv.reg[i] = (float)r[3 + i]; }
  pv.propCount = modelProps(md, pv.prop);
  pv.hud[0] = p.ias; pv.hud[1] = p.pos.y; pv.hud[2] = p.heading(); pv.hud[3] = p.mach;
  pv.hud2[0] = p.gLoad; pv.hud2[1] = p.ctl.throttle; pv.hud2[2] = p.spec && p.spec->special == 1 ? jetNozzleAngle(p) / (0.5f * PI) : p.nozzle;   // XR-30: pitch vectoring (90 deg units)
  pv.hud2[3] = p.gear > 0.5f ? 1.f : 0.f;
  vec3 vb = length(p.vel) > 2.f ? p.q.conj().rotate(normalize(p.vel)) : vec3(0, 0, -1);
  pv.hudV[0] = vb.x; pv.hudV[1] = vb.y; pv.hudV[2] = vb.z;
  pv.hud3[0] = p.engineSpool; pv.hud3[1] = p.alpha / DEG; pv.hud3[2] = p.vel.y; pv.hud3[3] = p.agl();
  pv.exhaust = buildExhaustVisual(p, md);
  pv.exhaust.style = s.special == 2 ? 1.f : 0.f;
  pv.flame[0] = pv.flame[1] = 0.f;
  for (int i = 0; i < pv.exhaust.count; ++i) {
    pv.flame[0] = std::max(pv.flame[0], pv.exhaust.power[i][0]);
    pv.flame[1] = std::max(pv.flame[1], pv.exhaust.power[i][1]);
  }
  pv.flame[2] = s.special == 1 ? jetNozzleAngle(p) : 0.f; pv.flame[3] = p.mach;
  if (s.special == 3) {
    // the XR-50's state for its own build (plane_sdf.glsl mapMorpheus, plane_parts.glsl mrPartPose read it as gWr): the
    // wing's sweep, its flaps, the lift fan's doors and its fan; the nozzles' swivel and vectoring; the rocket's flame,
    // the leading edges' glow (from 900 C), the ram and scram combustion's shares; the reaction jets
    const float tb = p.tbcc[0] + p.tbcc[1] + p.tbcc[2];
    pv.wr[0][0] = p.sweep; pv.wr[0][1] = p.hiLift; pv.wr[0][2] = p.fanClutch; pv.wr[0][3] = p.fanAngle;
    pv.wr[1][0] = p.nzSwivel; pv.wr[1][1] = p.nzPitch[0]; pv.wr[1][2] = p.nzPitch[1]; pv.wr[1][3] = 0.5f * (p.nzYaw[0] + p.nzYaw[1]);
    pv.wr[2][0] = clampf(p.rocketN / kMorpheusRocketVac, 0.f, 1.f); pv.wr[2][1] = clampf((p.skinT - 1173.f) / (kMorpheusSkinLimit - 1173.f), 0.f, 1.2f);
    pv.wr[2][2] = tb > 1.f ? p.tbcc[2] / tb : 0.f; pv.wr[2][3] = tb > 1.f ? p.tbcc[1] / tb : 0.f;
    pv.wr[3][0] = p.rcs.x; pv.wr[3][1] = p.rcs.y; pv.wr[3][2] = p.rcs.z;
    // (outside, the surfaces as its fly-by-wire deflects them; in the cockpit the stick as the pilot holds it)
    if (!inside && p.flightTime > 0.f) { pv.Ctl[0] = p.surf.x; pv.Ctl[1] = p.surf.z; pv.Ctl[2] = p.surf.y; }
  }
}

FrameParams Game::buildFrame() {
  FrameParams fp;
  fp.time = realTime; fp.dt = std::max(lastDt, 1e-4f);
  computeSun(timeOfDay, fp.sunDir, fp.sunCol, fp.night, plane.pos.y, &fp.sunDim);   // (the sun on the aircraft; the lighting pass works out the rest place by place)
  {   // the stars and the moon (sky.h): the sky turning with the sun's hour, the moon at today's phase
    const SkyBodies sb = skyBodiesAt(timeOfDay);
    for (int i = 0; i < 9; i++) fp.skyRot[i] = sb.toEquator[i];
    fp.moonDir = sb.moonDir; fp.moonLit = sb.moonLit;
  }
  {   // terrain's soft sun shadow at the aircraft: the same march as the shader's terrainShadow, done once here instead
      // of for every pixel of the airframe and cockpit
    float res = 1.f, t = 2.f;
    vec3 ro = plane.pos;
    if (fp.sunDir.y > -0.05f)
      for (int i = 0; i < 40; i++) {
        vec3 q = ro + fp.sunDir * t;
        if (q.y > g_ren.terrainCeiling()) break;
        float h = q.y - g_world.height(q.x, q.z, 4);
        res = std::min(res, 12.f * h / t);
        if (res < 0.f) { res = 0.f; break; }
        t += clampf(h * 0.6f, 6.f, 450.f);
      }
    fp.planeTerrSh = clampf(res, 0.f, 1.f);
  }
  fp.cloudCover = wx.cloudCover; fp.cloudBase = wx.cloudBase;
  // (in the rain the visibility follows how hard it is raining here: the heavier cells close it in)
  const bool flying = screen == SCR_FLIGHT && plane.spec;
  fp.fogB = 1.5f / std::max(wx.visibility * (flying && wx.precip ? 1.25f - 0.5f * rainNow : 1.f), 500.f);
  fp.wet = wx.precip == 1 ? 1.f : 0.f; fp.snow = wx.precip == 2 ? 0.8f : 0.f;
  fp.storm = wx.storm ? 1.f : 0.f; fp.lightning = lightning; fp.lightningTop = lightningTop; fp.lightningGlow = lightningGlow;
  fp.windOff = cloudOff; fp.cloudDet = cloudDet; fp.cloudBoil = cloudBoil;
  fp.wind = vec3(-sinf(wx.windFrom * DEG), 0, cosf(wx.windFrom * DEG)) * wx.windSpeed;
  fp.windSock = fp.wind;
  if (flying) {   // the windsocks swing with the gusts coming through (the bursts the aircraft meets, where the camera is)
    wxfield::Local calm; float g = std::max(g_world.height(camPos.x, camPos.z, 3), 0.f);
    fp.windSock = wxfield::wind(wx, calm, vec3(camPos.x, g + 8.f, camPos.z), 8.f, gameTime, plane.wxAir, 10.f).v;
    fp.windSock.y = 0;
  }
  // the aircraft's wake through the cloud: its points where the cloud field has carried them, the channel's radius
  // (the pair's oval, about its spacing either side, spreading with the turbulence as it ages), how far the vortex
  // pair has sunk, the distance along the path in Crow wavelengths (about 8.6 core spacings), the age, the pair as it
  // was there, and each segment's strength (opening in a moment, filling in over a minute, faded at the oldest end;
  // none across a gap)
  fp.wakeN = 0;
  static const bool wakeOff = getenv("WAKEOFF") != nullptr;   // (debug A/B: no wake through the cloud)
  if (flying && !wakeOff && !cloudWake.empty() && wx.cloudCover >= 0.02f) {
    std::vector<WakePt> pts = cloudWake;
    if (!wakeGap && !crashed) { const WakePt L = pts.back(); const vec3 hp = plane.pos + vec3(cloudOff.x, 0, cloudOff.y); pts.push_back(wakeHere(hp, false, L.odo + length(hp - L.c))); }
    const int n = std::min((int)pts.size(), FrameParams::kWakeMax), o = (int)pts.size() - n;
    const float span = plane.spec->span, crowL = 6.75f * span;
    vec3 lo(1e9f), hi(-1e9f); float rMax = 0, sMax = 0;
    for (int i = 0; i < n; i++) {
      const WakePt& w = pts[o + i];
      vec3 p = w.c - vec3(cloudOff.x, 0, cloudOff.y);
      float r = w.b0 * (1.05f + 0.2f * sqrtf(w.age)) + 1.f;
      float sink = clampf(w.w0 * w.T * (1.f - expf(-w.age / w.T)), -4.f * span, 4.f * span);
      fp.wake[i][0] = p.x; fp.wake[i][1] = p.y; fp.wake[i][2] = p.z; fp.wake[i][3] = r;
      fp.wakeP[i][1] = sink; fp.wakeP[i][2] = w.odo / crowL; fp.wakeP[i][3] = w.age;
      fp.wakeG[i][0] = w.gam; fp.wakeG[i][1] = w.b0; fp.wakeG[i][2] = w.tilt; fp.wakeG[i][3] = w.py;
      lo = vec3(std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)); hi = vec3(std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)); rMax = std::max(rMax, r); sMax = std::max(sMax, fabsf(sink));
      if (i + 1 < n) {
        const WakePt& v = pts[o + i + 1];
        float age = 0.5f * (w.age + v.age);
        fp.wakeP[i][0] = v.brk ? 0.f : smoothstepf(0.f, 0.7f, age) * std::min(1.f, 1.25f * expf(-age / 45.f)) * smoothstepf(0.f, 2.f, (float)i);
      } else fp.wakeP[i][0] = 0.f;
    }
    vec3 cen = (lo + hi) * 0.5f;
    fp.wakeB[0] = cen.x; fp.wakeB[1] = cen.y; fp.wakeB[2] = cen.z; fp.wakeB[3] = length(hi - lo) * 0.5f + 2.2f * rMax + sMax;   // (clouds.glsl: a point as far as 2.2 radii out may be in the channel's turbulent wall, the pair sunk away from the path)
    fp.wakeN = n;
    // the airframe's vortices and engines now (the young wake's: older, only the pair matters), from the pair's centre
    const AeroWake& wk = plane.wake;
    fp.wakeVN = fp.wakeEN = 0;
    for (int k = 0; k < wk.nv && fp.wakeVN < 10; k++) {
      const AeroWake::Vortex& v = wk.v[k];
      const bool winds = v.surf == AS_WING && v.gam * v.r.x * wk.gamPair > 0.f;   // (a wing vortex turning as its side's tip's: it winds into it)
      float* q = fp.wakeV[fp.wakeVN++]; q[0] = v.r.x; q[1] = v.r.y - wk.pairY; q[2] = v.gam; q[3] = winds ? 1.f : 0.f;
    }
    for (int e = 0; e < wk.nj && fp.wakeEN < 4; e++) {
      const AeroWake::Jet& j = wk.j[e];
      float* q = fp.wakeE[fp.wakeEN++]; q[0] = j.r.x; q[1] = j.r.y; q[2] = j.hot ? -j.R : j.R; q[3] = j.swirl;
    }
    fp.wakeA[0] = wk.gamPair; fp.wakeA[1] = wk.b0; fp.wakeA[2] = 7.f * std::max(wk.span, 1.f) / std::max(wk.V, 20.f); fp.wakeA[3] = 0.f;
    static const bool wakeDbg = getenv("WAKEDBG") != nullptr;   // (debug: the wake handed to the cloud pass)
    if (wakeDbg) {
      printf("wake: %d points, sphere r %.0f\n", n, fp.wakeB[3]);
      for (int i = 0; i < n; i++) printf("  %2d: %+7.0f %+6.0f %+7.0f from the aircraft, r %4.1f, K %.2f, sunk %4.1f, age %4.1f, density %.2f\n", i, fp.wake[i][0] - plane.pos.x, fp.wake[i][1] - plane.pos.y, fp.wake[i][2] - plane.pos.z, fp.wake[i][3], fp.wakeP[i][0], fp.wakeP[i][1], fp.wakeP[i][3], wxfield::cloudDensity(wx, vec3(fp.wake[i][0], fp.wake[i][1], fp.wake[i][2]), true));
    }
  }
  fp.exposure = 1.0f + fp.night * 0.8f;
  if ((screen == SCR_FLIGHT || screen == SCR_DEBRIEF || screen == SCR_LOADING) && plane.spec) {
    fillPlaneVisual(fp.plane, plane, propAngle, camMode == 1);
    if (camMode == 1 && wreck.empty()) {   // cockpit view: draw this frame's display / gauge atlas
      fp.dispCk = kModels[typeOf(plane.spec)].cockpit;
      fp.dispMode = plane.spec->special ? 1 : fp.dispCk == 2 ? 3 : 2;   // glass cockpits: panel + the centre display page
    }
    {   // transonic vapour cone: strongest just below Mach 1 in humid low-level air
      float M = plane.mach, humid = clampf(0.35f + 0.45f * wx.cloudCover + (wx.precip ? 0.3f : 0.f), 0.f, 1.f) * smoothstepf(11000.f, 1500.f, plane.pos.y);
      float k = smoothstepf(0.9f, 0.965f, M) * smoothstepf(1.07f, 1.0f, M) * humid * (plane.onGround || crashed ? 0.f : 1.f);
      float span = plane.spec->span;
      fp.plane.vapor[0] = k; fp.plane.vapor[1] = -1.f; fp.plane.vapor[2] = span * 0.2f; fp.plane.vapor[3] = span * 0.7f;
    }
    if (!wreck.empty()) {
      WreckVisual& wv = fp.wreck;
      static_assert(kWreckPieces == kMaxBreakPieces && kWreckCuts == kMaxBreakCuts, "the renderer's wreck limits are the breakup's");
      wv.pieces = std::min((int)wreck.size(), kWreckPieces);
      std::fill(std::begin(wv.gearOwner), std::end(wv.gearOwner), -1);
      for (int i = 0; i < wv.pieces; i++) {
        const WreckPiece& w = wreck[i];
        wv.pos[i] = w.c - w.q.rotate(w.body.cg); wv.C[i] = w.C; wv.H[i] = w.rigidOnly ? vec3(-1.f) : w.H;
        if (w.kind == BK_GEAR) wv.gearOwner[w.side < 0 ? 0 : w.side > 0 ? 1 : 2] = i;
        wv.mid[i] = wv.pos[i] + w.q.rotate((w.body.lo + w.body.hi) * 0.5f); wv.rad[i] = 0.5f * length(w.body.hi - w.body.lo) + 0.5f;
        wv.cutN[i] = w.cutN;
        wv.burn[i] = clampf(w.fire * (0.15f + crashTimer / (airBreak ? 8.f : 2.f)), 0.f, 1.f);   // (an impact's fireball chars at once; a failure in the air as the pieces burn)
        for (int k = 0; k < w.cutN; k++) { const BreakCut& c = w.cut[k]; float* o = wv.cut[i][k]; o[0] = c.C.x; o[1] = c.C.y; o[2] = c.C.z; o[3] = c.H.x; o[4] = c.H.y; o[5] = c.H.z; }
        vec3 ax = w.q.rotate(vec3(1, 0, 0)), ay = w.q.rotate(vec3(0, 1, 0)), az = w.q.rotate(vec3(0, 0, 1));
        float r[9] = {ax.x, ax.y, ax.z, ay.x, ay.y, ay.z, az.x, az.y, az.z};
        memcpy(wv.rot[i], r, sizeof r);
      }
    }
    fp.wreck.debris = std::min((int)debris.size(), 16);
    for (int i = 0; i < fp.wreck.debris; i++) {
      const Debris& d = debris[i];
      fp.wreck.deb[i][0] = d.p.x; fp.wreck.deb[i][1] = d.p.y; fp.wreck.deb[i][2] = d.p.z; fp.wreck.deb[i][3] = d.charred ? -d.size : d.size;
      fp.wreck.debQ[i][0] = d.q.w; fp.wreck.debQ[i][1] = d.q.x; fp.wreck.debQ[i][2] = d.q.y; fp.wreck.debQ[i][3] = d.q.z;
    }
    refreshGroundPits();   // (the pits drawn are the pits the aircraft, the weapons and the wreckage meet)
    fp.wreck.craterN = 0;
    for (const GroundPit& p : g_groundPits) { float* c = fp.wreck.crater[fp.wreck.craterN++]; c[0] = p.x; c[1] = p.z; c[2] = p.R; c[3] = p.D; }
    vec3 fwd = camMode == 1 ? plane.q.rotate(quat::axisAngle(vec3(0, 1, 0), lookYaw).rotate(quat::axisAngle(vec3(1, 0, 0), lookPitch).rotate(vec3(0, 0, -1))))
                            : normalize(plane.pos + vec3(0, plane.spec->fusRad * 0.3f, 0) - camPos);
    vec3 upRef = camMode == 1 ? plane.up() : vec3(0, 1, 0);
    if (camMode == 0) upRef = normalize(lerp(vec3(0, 1, 0), camQ.rotate(vec3(0, 1, 0)), 0.3f));
    if (photo.on) { camPos = photo.pos; fwd = photoForward(); upRef = photoUp(); }   // (photo mode's free camera)
    if (dbgCam && dbgFollow) { dbgCamPos = plane.pos + dbgFollowOff; dbgCamLook = plane.pos; }
    if (dbgCam) { camPos = dbgCamPos; fwd = normalize(dbgCamLook - dbgCamPos); upRef = vec3(0, 1, 0); }
    const bool station = camMode == 1 && plane.spec && plane.spec->special == 3;   // (the XR-50's ground station: the room never moves)
    if (screen == SCR_FLIGHT && !dbgCam && !photo.on && camMode != 3 && !station && set.camShake > 0.f) {   // (the shake: small turns of the view, 2-5 Hz)
      const float a = (shakeTrauma * shakeTrauma + shakeBuffet) * set.camShake * (camMode == 1 ? 0.7f : 1.f);
      if (a > 1e-4f) {
        const float t = shakeT;
        auto n = [&](float f, float ph) { return sinf(t * f + ph) * 0.6f + sinf(t * f * 2.31f + ph * 1.7f) * 0.4f; };
        const vec3 right = normalize(cross(fwd, upRef));
        fwd = normalize(fwd + right * (n(23.f, 0.f) * a * 1.1f * DEG) + upRef * (n(29.f, 1.3f) * a * 1.4f * DEG));
        upRef = normalize(upRef + right * (n(19.f, 2.1f) * a * 0.9f * DEG));
      }
    }
    fp.camPos = camPos;
    fp.camBack = -fwd;
    fp.camRight = normalize(cross(fwd, upRef));
    fp.camUp = cross(fp.camRight, fwd);
    fp.fovY = (camMode == 1 ? set.fov + 19.f : set.fov) * DEG;
    if (camMode == 3) fp.fovY = clampf(2.f * atanf(std::max(plane.spec->span, plane.spec->fusLen) * (botControl ? 0.42f : 1.5f) / length(plane.pos - camPos)), 4.f * DEG, 60.f * DEG);
    if (camMode == 1) fp.fovY = 2.f * atanf(tanf((set.fov + 19.f) * 0.5f * DEG) / ckZoom);   // cockpit zoom: lean in to read the displays
    if (camMode == 0 && set.speedFov && screen == SCR_FLIGHT && plane.spec)   // (the chase view widens a little with speed: up to 5 deg past cruise)
      fp.fovY += smoothstepf(0.6f, 1.6f, length(plane.vel) / std::max(plane.spec->cruise, 20.f)) * 5.f * DEG;
    if (screen == SCR_LOADING && militaryFlight) militaryLoadingCamera(fp);
    if (photo.on) {   // (photo mode: its lens, exposure, vignette, grade and frame)
      fp.fovY = photo.fov * DEG;
      fp.exposure *= exp2f(photo.ev);
      fp.vignette = photo.vignette;
      fp.grade[0] = photo.saturation; fp.grade[1] = photo.contrast; fp.grade[2] = photo.warmth;
      const float aspect = (float)std::max(g_ren.W, 1) / std::max(g_ren.H, 1);
      static const float kFrame[4] = {0.f, 2.39f, 1.85f, 1.f};   // (none, the cinema's, the wide screen's, a square)
      const float want = kFrame[std::clamp(photo.frame, 0, 3)];
      fp.bars[0] = want > 0.f && want < aspect ? 0.5f * (1.f - want / aspect) : 0.f;
      fp.bars[1] = want > 0.f && want > aspect ? 0.5f * (1.f - aspect / want) : 0.f;
    }
    g_audio.setView(fp.camPos,fp.camRight,fp.camUp,-fp.camBack);
    fp.landLight = landingLight && plane.engineRunning ? (0.3f + 0.7f * fp.night) : 0.f;
    fp.landLightPos = plane.pos + plane.forward() * (plane.spec->fusLen * 0.4f);
    fp.landLightDir = normalize(plane.forward() - plane.up() * 0.1f);
    somnusVisual(fp);
    if (!wreck.empty()) {
      // Freeze articulation while preserving the moving world frame:
      // nearby-wreck shadow selection still needs its current position.
      const vec3 worldPos = fp.plane.pos;
      float worldRot[9]; memcpy(worldRot, fp.plane.rot, sizeof worldRot);
      fp.plane = wreckPlanePose; fp.plane.pos = worldPos;
      memcpy(fp.plane.rot, worldRot, sizeof worldRot);
    }
    if (!(screen == SCR_LOADING && militaryFlight)) buildFeedCameras(fp);
    if (screen == SCR_LOADING && !headless) fp.warmCockpit = feedRigOf(plane.spec);   // (the cockpit ready before the flight opens: raster_renderer.cpp)
    if (boomT >= 0 && boomT < 1.6f && !(length(fp.flameLight) > 0.f)) {   // an explosion's flash lights the scene, fading as the fireball cools
      float k = boomT < 0.08f ? boomT / 0.08f : expf(-(boomT - 0.08f) * 2.6f);
      fp.flameLightPos = boomP; fp.flameLight = vec3(1.f, 0.6f, 0.28f) * (boomI * k);
    }
    buildLights(fp);
    {   // airport lighting: on at night and in low visibility
      float lowVis = smoothstepf(8000.f, 2000.f, wx.visibility) + (wx.cloudCover > 0.8f ? 0.3f : 0.f);
      float li = clampf(fp.night + lowVis * 0.6f + 0.12f, 0, 1);
      fp.rwyLights = li > 0.15f ? li : 0.f;
    }
    // (the scenery the craters flattened: refreshGroundPits)
    // the windscreen (cockpit view): the rain here and the cloud's mist on it, streaming away on screen from where
    // the air meets the glass - a little below where the nose points, so over the roof; out of a side window, aft
    fp.rainLens = 0; fp.glassMist = 0;
    // (not in the research jets' sealed pods: their windows are displays, and with no glass to bead on, the drops
    // streamed over the cabin's own frames and panels - more than 1.3 m from the eye, the depth the post pass takes
    // for "seen through the glass" - in the XR-40 at night in a storm)
    if (camMode == 1 && !crashed && !plane.spec->special && !(screen == SCR_LOADING && militaryFlight)) {
      fp.rainLens = wx.precip == 1 ? rainNow : 0.f;
      fp.glassMist = mistNow;
      vec3 f = normalize(plane.forward() - plane.up() * 0.45f);
      float x = dot(f, fp.camRight), y = dot(f, fp.camUp), z = dot(f, -fp.camBack);
      float ty = tanf(fp.fovY * 0.5f), tx = ty * (float)g_ren.W / std::max(g_ren.H, 1);
      float sign = 1.f;
      if (z < 0.f) { x = -x; y = -y; z = -z; sign = -1.f; }   // (the source behind: the drops run towards the point ahead the air goes)
      vec2 ndc = z > 0.02f ? vec2(x / z / tx, y / z / ty) : vec2(x, y) * (1.f / std::max(length(vec2(x, y)), 1e-3f)) * 50.f;
      if (length(ndc) > 50.f) ndc = ndc * (50.f / length(ndc));
      fp.rainFlow[0] = 0.5f + 0.5f * ndc.x; fp.rainFlow[1] = 0.5f + 0.5f * ndc.y; fp.rainFlow[2] = sign;
      fp.rainFlow[3] = smoothstepf(6.f, 45.f, plane.airspeed);
    }
    fp.sealedCockpit = camMode == 1 && plane.spec->special && !crashed && !(screen == SCR_LOADING && militaryFlight);
    fp.trafficN = traffic.fillVisuals(fp.camPos, fp.traffic, kMaxTrafficDrawn, nullptr);
    fp.ufoOn = ufo.on && length(ufo.pos - fp.camPos) < 20000.f;
    if (fp.ufoOn) {
      fp.ufoPos = ufo.pos;
      float m[9] = {ufo.right.x, ufo.right.y, ufo.right.z, ufo.up.x, ufo.up.y, ufo.up.z, ufo.fwd.x, ufo.fwd.y, ufo.fwd.z};
      memcpy(fp.ufoRot, m, sizeof m);
      fp.ufoAnim[0] = ufo.hatch; fp.ufoAnim[1] = ufo.t; fp.ufoAnim[2] = ufo.laugh; fp.ufoAnim[3] = ufo.wave;
    }
    if (crashed) fp.fade = clampf(1.f - (crashTimer - (crashEndT - 1.f)), 0, 1);
    fp.gLoad = gTunnel;
  } else {
    menuBackgroundCamera(fp);
  }
  return fp;
}

// The blended sprites far to near, all of them together: the volcano's plume came after the sorted smoke and a distant
// plume drew over nearer smoke and trails (the review of v3.44.0, RND-5). Each quad is six vertices, placed by its middle
void sortAlphaQuads(std::vector<SpriteVert>& v, vec3 cam) {
  const size_t n = v.size() / 6;
  if (n < 2) return;
  std::vector<std::pair<float, uint32_t>> order(n);
  for (size_t i = 0; i < n; i++) {
    const SpriteVert* q = &v[i * 6];
    const vec3 c((q[0].x + q[1].x + q[2].x + q[5].x) * 0.25f, (q[0].y + q[1].y + q[2].y + q[5].y) * 0.25f, (q[0].z + q[1].z + q[2].z + q[5].z) * 0.25f);
    order[i] = {-length(c - cam), (uint32_t)i};
  }
  std::stable_sort(order.begin(), order.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
  std::vector<SpriteVert> out; out.reserve(v.size());
  for (auto& o : order) out.insert(out.end(), v.begin() + o.second * 6, v.begin() + o.second * 6 + 6);
  out.insert(out.end(), v.begin() + n * 6, v.end());   // (a tail that isn't a whole quad, as it was)
  v.swap(out);
}

void Game::buildSprites(const FrameParams& fp, std::vector<SpriteVert>& alpha, std::vector<SpriteVert>& add) {
  vec3 cr = fp.camRight;
  // billboards are turned to face whichever camera draws them (the main view or a cockpit camera) in the vertex shader
  // (kind: the sprite's kind, and in its fraction - under 0.4 - a value of its own: a puff's pattern, a splash's age)
  auto bill = [&](std::vector<SpriteVert>& v, vec3 p, float s, vec3 col, float a, float kind, float soft) {
    SpriteVert q[4] = {{p.x, p.y, p.z, 0, 0, col.x, col.y, col.z, a, kind, soft, s}, {p.x, p.y, p.z, 1, 0, col.x, col.y, col.z, a, kind, soft, s},
                       {p.x, p.y, p.z, 1, 1, col.x, col.y, col.z, a, kind, soft, s}, {p.x, p.y, p.z, 0, 1, col.x, col.y, col.z, a, kind, soft, s}};
    v.push_back(q[0]); v.push_back(q[1]); v.push_back(q[2]); v.push_back(q[0]); v.push_back(q[2]); v.push_back(q[3]);
  };
  auto quadAx = [&](std::vector<SpriteVert>& v, vec3 p, vec3 ax, vec3 ay, vec3 col, float a, float kind, float soft) {
    vec3 c0 = p - ax - ay, c1 = p + ax - ay, c2 = p + ax + ay, c3 = p - ax + ay;
    SpriteVert q[4] = {{c0.x, c0.y, c0.z, 0, 0, col.x, col.y, col.z, a, kind, soft}, {c1.x, c1.y, c1.z, 1, 0, col.x, col.y, col.z, a, kind, soft},
                       {c2.x, c2.y, c2.z, 1, 1, col.x, col.y, col.z, a, kind, soft}, {c3.x, c3.y, c3.z, 0, 1, col.x, col.y, col.z, a, kind, soft}};
    v.push_back(q[0]); v.push_back(q[1]); v.push_back(q[2]); v.push_back(q[0]); v.push_back(q[2]); v.push_back(q[3]);
  };
  float night = fp.night;
  float lowVis = smoothstepf(8000.f, 2000.f, wx.visibility) + (wx.cloudCover > 0.8f ? 0.3f : 0.f);
  float lightI = clampf(night + lowVis * 0.6f + 0.12f, 0, 1);
  // the traffic's lamps at range (road_traffic.h): two headlights white, two tail lights red, a pixel's point each - up
  // close the modelled lamps are the light (ent_fs2.glsl)
  if (night > 0.05f) {
    const vec2 off(fp.camPos.x - g_world.wrap(fp.camPos.x), fp.camPos.z - g_world.wrap(fp.camPos.z));   // (in the copy of the islands the camera is over)
    for (const RoadCar& c : g_roadTraffic.all()) {
      vec3 at; vec2 h;
      if (!g_roadTraffic.place(g_world, c, at, h)) continue;
      at.x += off.x; at.z += off.y;
      const float dl = length(at - fp.camPos), fade = smoothstepf(40.f, 160.f, dl);
      if (fade <= 0.f || dl > RoadTraffic::kDrawn + 400.f) continue;
      const float L = c.kind == EK_CAR ? 2.2f : 8.3f;
      const vec3 f(h.x, 0.f, h.y), r(-h.y, 0.f, h.x);
      for (float s : {-0.6f, 0.6f}) {
        bill(add, at + f * L + r * s + vec3(0, 0.75f, 0), dl * 0.0016f, vec3(1.f, 0.93f, 0.8f) * (5.f * night * fade), 1.f, SPR_GLOW, 2.f);
        bill(add, at - f * L + r * s + vec3(0, 0.9f, 0), dl * 0.0013f, vec3(1.f, 0.05f, 0.02f) * (4.f * night * fade), 1.f, SPR_GLOW, 2.f);
      }
    }
    // the ferries' navigation lights (scenery.h): her two masthead lights white, at the end she's heading for a sidelight
    // either side (red to port, green to starboard), a white stern light at the other; out to 15 km
    for (const FerryRoute& fr : ferryRoutes()) {
      vec2 p0, p1, h; bool docked = false;
      if (!ferryAt(fr, fp.time, p0, h, &docked) || !ferryAt(fr, fp.time + 1.f, p1, h)) continue;
      const vec3 at(p0.x + off.x, 0.f, p0.y + off.y);
      const float dl = length(at - fp.camPos), fade = smoothstepf(60.f, 300.f, dl);
      if (fade <= 0.f || dl > 15000.f) continue;
      const vec2 mv = p1 - p0; const vec2 g = length(mv) > 0.1f ? mv * (1.f / length(mv)) : h;   // (the way she's going)
      const vec3 f(g.x, 0.f, g.y), r(-g.y, 0.f, g.x), hz(h.x, 0.f, h.y);
      const float sz = dl * 0.0016f, I = 5.f * night * fade;
      for (float s : {-29.f, 29.f}) bill(add, at + hz * s + vec3(0, 26.3f, 0), sz, vec3(1.f, 0.95f, 0.85f) * I, 1.f, SPR_GLOW, 2.f);
      bill(add, at + f * 29.f - r * 8.3f + vec3(0, 18.5f, 0), sz, vec3(1.f, 0.06f, 0.03f) * I, 1.f, SPR_GLOW, 2.f);
      bill(add, at + f * 29.f + r * 8.3f + vec3(0, 18.5f, 0), sz, vec3(0.1f, 1.f, 0.35f) * I, 1.f, SPR_GLOW, 2.f);
      bill(add, at - f * 32.5f + vec3(0, 16.f, 0), sz, vec3(1.f, 0.95f, 0.85f) * (0.8f * I), 1.f, SPR_GLOW, 2.f);
    }
    // and the yachts' anchor lights at their mastheads, out to 4 km
    for (const PlacedEnt& p : landmarks()) {
      if (p.kind != EK_BOAT) continue;
      const vec3 at(p.e.x + sinf(p.e.yaw) * p.e.sz + off.x, 14.1f * p.e.sy, p.e.z + cosf(p.e.yaw) * p.e.sz + off.y);
      const float dl = length(at - fp.camPos), fade = smoothstepf(40.f, 160.f, dl);
      if (fade <= 0.f || dl > 4000.f) continue;
      bill(add, at, dl * 0.0012f, vec3(1.f, 0.95f, 0.85f) * (3.f * night * fade), 1.f, SPR_GLOW, 2.f);
    }
  }
  // runway lighting
  for (size_t ai = 0; ai < g_world.airports.size(); ai++) {
    const Airport& a = g_world.airports[ai];
    float d = length(a.pos() - fp.camPos);
    if (d > 16000.f || a.water()) continue;   // (a seaplane base's lane: unlit, its buoys alone)
    vec3 dir = a.dir(), rt(-dir.z, 0, dir.x);
    if (lightI > 0.15f) {
      for (float u = -a.length * 0.5f; u <= a.length * 0.5f + 0.1f; u += 60.f)
        for (int sd = -1; sd <= 1; sd += 2) {
          vec3 p = a.pos() + dir * u + rt * (sd * (a.width * 0.5f + 1.5f)) + vec3(0, 0.4f, 0);
          vec3 col = fabsf(u) > a.length * 0.5f - 600.f && a.size > 0 ? vec3(1.f, 0.85f, 0.45f) : vec3(1.f, 0.92f, 0.75f);
          float dl = length(p - fp.camPos);   // glints at range; close up the modelled fixture and its pool are the light
          bill(add, p, dl * 0.0009f, col * (2.0f * lightI) * smoothstepf(60.f, 250.f, dl), 1.f, SPR_GLOW, 2.f);   // a pixel-sized point at range
        }
      AptLayout L = aptLayout(a, (int)ai);
      if (L.paved) {   // blue taxiway edge lights (the same positions as the fixtures in airport_scenery.cpp)
        auto blue = [&](float u, float vv) {
          vec3 p = aptWorld(a, u, L.side * vv, a.elev + 0.4f);
          float dl = length(p - fp.camPos);
          bill(add, p, dl * 0.0012f, vec3(0.2f, 0.35f, 1.f) * 2.2f * lightI * smoothstepf(60.f, 250.f, dl), 1.f, SPR_GLOW, 2.f);
        };
        float tEnd = a.length * 0.5f - 25.f + L.twHW;
        for (float tu = -tEnd; tu <= tEnd; tu += 60.f) {
          bool atExit = false;
          for (int e = 0; e < L.nExit; e++) if (fabsf(tu - L.exitU[e]) < L.twHW + 4.f) atExit = true;
          if (!atExit) blue(tu, L.twV - L.twHW - 1.f);
          if (!(tu > L.apU0 && tu < L.apU1)) blue(tu, L.twV + L.twHW + 1.f);
        }
      }
      for (int end = -1; end <= 1; end += 2) {
        for (float v = -a.width * 0.5f; v <= a.width * 0.5f; v += 3.f) {
          vec3 p = a.pos() + dir * (end * (a.length * 0.5f + 1.f)) + rt * v + vec3(0, 0.4f, 0);
          float dl = length(p - fp.camPos);
          bill(add, p, dl * 0.0009f, (end < 0 ? vec3(0.2f, 1.f, 0.3f) : vec3(1.f, 0.15f, 0.1f)) * 2.0f * lightI * smoothstepf(60.f, 250.f, dl), 1.f, SPR_GLOW, 2.f);
        }
        if (a.size > 0 && night > 0.3f)
          for (int k = 1; k <= 6; k++)
            for (float v = -8.f; v <= 8.f; v += 4.f) {
              vec3 p = a.pos() + dir * (end * (a.length * 0.5f + 60.f * k)) + rt * v;
              p.y = std::max(g_world.height(p.x, p.z), a.elev) + 1.f;
              float dl = length(p - fp.camPos);
              bill(add, p, dl * 0.0009f, vec3(1.f, 0.95f, 0.85f) * 2.5f * night * smoothstepf(60.f, 250.f, dl), 1.f, SPR_GLOW, 3.f);
            }
      }
    }
    // PAPI (always on) on the left of each landing direction, 300 m past the threshold
    if (a.size > 0 || a.surface == SURF_ASPHALT) {
      for (int end = -1; end <= 1; end += 2) {
        vec3 ldir = dir * (float)(-end);              // landing direction
        vec3 thr = a.pos() + dir * (end * a.length * 0.5f);
        vec3 lrt(-ldir.z, 0, ldir.x);
        vec3 base = thr + ldir * 300.f - lrt * (a.width * 0.5f + 15.f) + vec3(0, 0.6f, 0);
        vec3 toCam = fp.camPos - base;
        float horiz = length(vec3(toCam.x, 0, toCam.z));
        if (dot(vec3(toCam.x, 0, toCam.z), ldir) > 0) continue;  // only visible from the approach side
        float ang = atan2f(toCam.y, horiz) / DEG;
        for (int i = 0; i < 4; i++) {
          float th = 2.5f + i * 0.333f;   // outer light has the highest threshold
          bool white = ang > (3.5f - i * 0.333f);
          (void)th;
          vec3 p = base - lrt * (i * 9.f);
          float dl = length(p - fp.camPos);   // the modelled unit carries it up close; a pixel-sized point beyond
          bill(add, p, dl * 0.0009f, (white ? vec3(1.f, 0.95f, 0.9f) : vec3(1.f, 0.1f, 0.08f)) * 3.0f * smoothstepf(150.f, 500.f, dl), 1.f, SPR_GLOW, 3.f);
        }
      }
    }
  }
  if ((screen == SCR_FLIGHT || screen == SCR_LOADING) && plane.spec && !crashed) {   // nav light glints
    const AircraftSpec& s = *plane.spec;
    float t = realTime;
    float dcam = length(plane.pos - fp.camPos);
    float ls = std::max(0.05f, dcam * 0.0012f);   // distant glints only: up close the modelled lens is the light
    float glint = 0.2f + 0.8f * smoothstepf(20.f, 110.f, dcam);
    float navI = (0.6f + 2.5f * night) * (1.f - somnus.stealth) * glint;   // a cloaked XR-40 runs dark
    const ModelDef& md = kModels[typeOf(plane.spec)];
    bool jet = s.special != 0;   // the XR-30 is an SDF of its own: its lights don't follow the generic model layout
    const bool xr50 = s.special == 3;
    vec3 tip = xr50 ? morpheusWingTip(plane.sweep) : s.special == 2 ? kSomnusWingTip : jet ? kJetWingTip : modelWingTip(md);
    vec3 lt = plane.pos + plane.q.rotate(vec3(-tip.x, tip.y, tip.z)), rtp = plane.pos + plane.q.rotate(tip);
    if (camMode != 1) {
      bill(add, lt, ls, vec3(1.f, 0.1f, 0.05f) * navI, 1, SPR_GLOW, 0.3f);
      bill(add, rtp, ls, vec3(0.1f, 1.f, 0.2f) * navI, 1, SPR_GLOW, 0.3f);
      bill(add, plane.pos + plane.q.rotate(xr50 ? kMorpheusTail + vec3(0, 0, 0.07f) : s.special == 2 ? vec3(0, -0.1f, 7.93f) : jet ? vec3(0, 0.45f, 7.67f) : modelTailTip(md)), ls, vec3(1.f) * navI, 1, SPR_GLOW, 0.3f);
      if (plane.engineRunning && somnus.stealth < 0.5f && fmodf(t, 1.0f) < 0.12f) bill(add, plane.pos + plane.q.rotate(xr50 ? kMorpheusBeacon + vec3(0, 0.07f, 0) : s.special == 2 ? vec3(0, 0.6f, 1.6f) : jet ? vec3(0, 0.74f, 1.6f) : modelFinTop(md) + vec3(0, 0.06f, 0)), ls * 1.6f, vec3(1.f, 0.05f, 0.02f) * (2.f + 3.f * night) * glint, 1, SPR_GLOW, 0.3f);
      float st = fmodf(t, 1.3f);
      if (!plane.onGround && somnus.stealth < 0.5f && (st < 0.05f || (st > 0.12f && st < 0.16f))) { bill(add, lt, ls * 3.f, vec3(4.f) * glint, 1, SPR_GLOW, 0.3f); bill(add, rtp, ls * 3.f, vec3(4.f) * glint, 1, SPR_GLOW, 0.3f); }
    }
    // AI traffic lights: nav lights, beacon, strobes when airborne, landing lights on the runway and on approach,
    // reheat glow on XR-30 formations
    for (const TrafficCraft& c : traffic.craft) {
      float dc = length(c.pos - fp.camPos);
      if (dc > 9000.f) continue;
      const ModelDef& tm = kModels[c.spec];
      float ls2 = std::max(0.05f, dc * 0.0012f), g2 = 0.2f + 0.8f * smoothstepf(20.f, 110.f, dc);   // glints at range; close up the lenses glow
      vec3 tip = c.spec == kResearchJet ? kJetWingTip : modelWingTip(tm);
      vec3 lt = c.pos + c.q.rotate(vec3(-tip.x, tip.y, tip.z)), rt = c.pos + c.q.rotate(tip);
      float tnav = (0.6f + 2.5f * night) * g2;
      bill(add, lt, ls2, vec3(1.f, 0.1f, 0.05f) * tnav, 1, SPR_GLOW, 0.3f);
      bill(add, rt, ls2, vec3(0.1f, 1.f, 0.2f) * tnav, 1, SPR_GLOW, 0.3f);
      float ph = fmodf(t + c.id * 0.37f, 1.f);
      if (ph < 0.1f) bill(add, c.pos + c.q.rotate(vec3(0, kAircraft[c.spec].fusRad * 1.05f, 0)), ls2 * 1.4f, vec3(1.f, 0.05f, 0.02f) * (2.f + 3.f * night) * g2, 1, SPR_GLOW, 0.3f);
      bool airborne = c.state >= TrafficCraft::TAKEOFF && c.state <= TrafficCraft::ROLLOUT;
      if ((airborne || c.role != TrafficCraft::AIRPORT) && fmodf(t * 0.77f + c.id * 0.13f, 1.3f) < 0.05f) { bill(add, lt, ls2 * 2.5f, vec3(4.f) * g2, 1, SPR_GLOW, 0.3f); bill(add, rt, ls2 * 2.5f, vec3(4.f) * g2, 1, SPR_GLOW, 0.3f); }
      if (c.role == TrafficCraft::AIRPORT && (c.state == TrafficCraft::TAKEOFF || c.state == TrafficCraft::FINAL || c.state == TrafficCraft::ROLLOUT || c.state == TrafficCraft::LINEUP))
        bill(add, c.pos + c.q.rotate(vec3(0, -kAircraft[c.spec].fusRad * 0.6f, -kAircraft[c.spec].fusLen * 0.4f)), ls2 * 2.f, vec3(1.f, 0.95f, 0.85f) * (1.f + 4.f * night), 1, SPR_GLOW, 0.3f);
      if (c.ab > 0.5f) for (int k = -1; k <= 1; k += 2)
        bill(add, c.pos + c.q.rotate(vec3(k * 0.82f, -0.12f, 8.9f)), 0.3f + ls2, vec3(1.f, 0.65f, 0.35f) * 1.6f, 1, SPR_GLOW, 0.6f);
    }
    if (landingLight && plane.engineRunning && camMode != 1)
      bill(add, plane.pos + plane.q.rotate(vec3(s.engLayout == 0 ? -s.span * 0.25f : 0, s.engLayout == 0 ? s.wingY * s.fusRad : -s.fusRad * 0.6f, s.engLayout == 0 ? s.wingZ - s.chord * 0.5f : -0.35f * s.fusLen)), ls * 1.8f,
           vec3(1.f, 0.95f, 0.85f) * (1.f + 4.f * night) * glint, 1, SPR_GLOW, 0.3f);
    // the route from gate to gate (SPR_ROUTE): a curve leaving each gate the way it faces and arriving at the next the
    // same way (cubic Hermite, level at both ends: a climb or a descent between them is an S), drawn as a ribbon of
    // light turned to the camera - 8 m wide, never thinner than a few pixels - with chevrons flowing along it toward
    // the next gate (sprite_fs.glsl); it grows out of each gate's centre and fades as the camera nears it
    const float focal = 0.5f * g_ren.H / tanf(0.5f * std::max(fp.fovY, 0.05f));   // (pixels per metre at a metre)
    std::vector<vec3> rp; std::vector<float> rs;
    auto route = [&](vec3 p0, vec3 ax0, vec3 p1, vec3 ax1, vec3 col, float a) {
      const vec3 up(0, 1, 0);
      const float lh = length(vec3(p1.x - p0.x, 0, p1.z - p0.z));
      if (lh < 1.f) return;
      const vec3 t0 = normalize(cross(up, ax0)) * (lh * 0.9f), t1 = normalize(cross(up, ax1)) * (lh * 0.9f);
      const int n = std::min(420, std::max(32, (int)(lh / 9.f)));
      rp.resize(n + 1); rs.resize(n + 1);
      for (int k = 0; k <= n; k++) {
        const float u = (float)k / n, u2 = u * u, u3 = u2 * u;
        rp[k] = p0 * (2 * u3 - 3 * u2 + 1) + t0 * (u3 - 2 * u2 + u) + p1 * (3 * u2 - 2 * u3) + t1 * (u3 - u2);
        rs[k] = k ? rs[k - 1] + length(rp[k] - rp[k - 1]) : 0.f;
      }
      const float total = rs[n];
      vec3 side = normalize(cross(rp[1] - rp[0], up)), pl, pr; float pa = 0, pu = 0;
      for (int k = 0; k <= n; k++) {
        const vec3 d = rp[std::min(k + 1, n)] - rp[std::max(k - 1, 0)], view = rp[k] - fp.camPos;
        const float dist = length(view);
        vec3 sd = cross(d, view); const float sl = length(sd);
        if (sl > 1e-3f * length(d) * dist) side = sd * (1.f / sl);   // (seen end-on: the last side kept)
        const float hw = std::max(4.f, 2.5f * dist / focal);
        const vec3 l = rp[k] - side * hw, r = rp[k] + side * hw;
        const float fa = a * smoothstepf(0.f, 70.f, rs[k]) * smoothstepf(0.f, 70.f, total - rs[k]) * smoothstepf(20.f, 140.f, dist);
        const float uu = rs[k] / 30.f;   // (one chevron every 30 m)
        if (k && (fa > 0.f || pa > 0.f)) {
          SpriteVert q[4] = {{pl.x, pl.y, pl.z, pu, 0, col.x, col.y, col.z, pa, (float)SPR_ROUTE, 2.f}, {pr.x, pr.y, pr.z, pu, 1, col.x, col.y, col.z, pa, (float)SPR_ROUTE, 2.f},
                             {r.x, r.y, r.z, uu, 1, col.x, col.y, col.z, fa, (float)SPR_ROUTE, 2.f}, {l.x, l.y, l.z, uu, 0, col.x, col.y, col.z, fa, (float)SPR_ROUTE, 2.f}};
          alpha.push_back(q[0]); alpha.push_back(q[1]); alpha.push_back(q[2]); alpha.push_back(q[0]); alpha.push_back(q[2]); alpha.push_back(q[3]);
        }
        pl = l; pr = r; pa = fa; pu = uu;
      }
    };
    // the route: the leg being flown (from the gate just passed), brightest, then on through the next two gates (not in
    // photo mode's shots unless it shows them)
    const bool markers = !photo.on || photo.markers;
    for (int i = std::max(wpIndex, 1) - 1; markers && i < wpIndex + 2; i++) {
      vec3 c0, ax0, ay0, c1, ax1, ay1;
      if (ringGeom(i, c0, ax0, ay0) && ringGeom(i + 1, c1, ax1, ay1)) route(c0, ax0, c1, ax1, vec3(0.9f, 0.32f, 1.f), i < wpIndex ? 1.f : i == wpIndex ? 0.75f : 0.4f);
    }
    // checkpoint gates: the active one spins and breathes
    for (int i = wpIndex; markers && i < (int)contract.wps.size() && i < wpIndex + 3; i++) {
      vec3 c, ax, ay;
      ringGeom(i, c, ax, ay);
      bool act = i == wpIndex;
      float br = act ? 1.f + 0.035f * sinf(t * 2.6f) : 0.85f;
      vec3 col = act ? vec3(0.2f, 1.f, 0.5f) * 2.2f : vec3(0.85f, 0.4f, 1.f) * 1.1f;
      quadAx(add, c, ax * br, ay * br, col, act ? 1.f : 0.55f, SPR_RING, 5.f);
      if (act) quadAx(add, c, ax * 1.25f, ay * 1.25f, col * 0.35f, 0.2f, SPR_SHOCK, 5.f);   // faint outer halo
    }
    for (const RingBurst& b : bursts) {
      if (!markers) break;
      float k = b.t / 0.9f, s = 1.f + 2.2f * (1.f - (1.f - k) * (1.f - k));
      quadAx(add, b.c, b.ax * s, b.ay * s, b.col * 3.f, (1.f - k) * (1.f - k), SPR_SHOCK, 5.f);
      if (k < 0.35f && b.flash) bill(add, b.c, 90.f * (0.5f + k * 3.f), b.col * 2.f * (1.f - k / 0.35f), 1.f, SPR_GLOW, 5.f);
    }
  }
  // trail ribbons: camera-facing strips through trail points, soft across their width (wingtip vapour; the smoke
  // trailing from break-up pieces)
  auto ribbon = [&](const std::vector<TipPt>& tt, float life, float w0, float wg, vec3 lit, float aMax) {
    for (size_t i = 1; i < tt.size(); i++) {
      const TipPt &q0 = tt[i - 1], &q1 = tt[i];
      if (q0.seg != q1.seg) continue;
      auto edge = [&](const TipPt& q, vec3 dir, vec3& lo, vec3& hi, float& al) {
        vec3 vd = normalize(q.p - fp.camPos), side = cross(dir, vd);
        float sl = length(side); side = sl > 1e-4f ? side / sl : cr;
        float w = w0 + q.age * wg;
        lo = q.p - side * w; hi = q.p + side * w;
        float lf = 1.f - q.age / life;
        al = aMax * q.a * lf * lf * smoothstepf(0.f, 0.05f, q.age) * smoothstepf(0.3f, 4.f, length(q.p - fp.camPos));
      };
      vec3 dir = q1.p - q0.p; float dl = length(dir);
      if (dl < 1e-3f) continue;
      dir = dir / dl;
      vec3 a0, b0, a1, b1; float al0, al1;
      edge(q0, dir, a0, b0, al0); edge(q1, dir, a1, b1, al1);
      if (al0 + al1 < 0.002f) continue;
      SpriteVert v0 = {a0.x, a0.y, a0.z, 0.5f, 0, lit.x, lit.y, lit.z, al0, 8.f, 1.5f}, v1 = {b0.x, b0.y, b0.z, 0.5f, 1, lit.x, lit.y, lit.z, al0, 8.f, 1.5f};
      SpriteVert v2 = {b1.x, b1.y, b1.z, 0.5f, 1, lit.x, lit.y, lit.z, al1, 8.f, 1.5f}, v3 = {a1.x, a1.y, a1.z, 0.5f, 0, lit.x, lit.y, lit.z, al1, 8.f, 1.5f};
      alpha.push_back(v0); alpha.push_back(v1); alpha.push_back(v2); alpha.push_back(v0); alpha.push_back(v2); alpha.push_back(v3);
    }
  };
  for (int sd = 0; sd < 2; sd++) ribbon(tipTrail[sd], 1.8f, 0.22f, 0.9f, vec3(1.f), 0.26f);
  for (int k = 0; k < kMaxBreakPieces; k++) ribbon(pieceTrail[k], 7.f, 0.9f, 2.4f, vec3(0.07f, 0.065f, 0.06f), 0.75f);
  // break-up pieces burning as they fall: a flame jet streaming a few metres off each one
  if (airBreak) {
    for (const WreckPiece& w : wreck) {
      if (w.landed) continue;
      float sp = length(w.v); if (sp < 1.f) continue;
      float heat = w.fire * clampf(1.3f - crashTimer * 0.08f, 0.35f, 1.f);
      vec3 vd = w.v / sp, toCam = normalize(fp.camPos - w.c);
      vec3 ax = vd - toCam * dot(vd, toCam); float al = length(ax);
      ax = al > 0.05f ? ax / al : cr;
      vec3 ay = normalize(cross(ax, toCam));
      float flick = 0.85f + 0.15f * sinf(realTime * 37.f + w.C.x * 13.f);
      float L = clampf(sp * 0.025f, 2.5f, 11.f) * heat * flick * std::max(al, 0.3f), Wd = (0.45f + 0.35f * w.fire) * heat;
      quadAx(add, w.c - ax * (L * 0.5f), ax * (L * 0.5f + Wd), ay * Wd, vec3(1.f, 0.55f, 0.18f) * 2.6f, heat, SPR_SPARK, 1.f);
      quadAx(add, w.c - ax * (L * 0.35f), ax * (L * 0.35f + Wd * 2.f), ay * Wd * 2.2f, vec3(1.f, 0.4f, 0.1f) * 0.8f, heat * 0.6f, SPR_SPARK, 1.f);
    }
  }
  // lightning: each channel segment a ribbon turned to the camera (its glow at least a few pixels wide however far off),
  // the stepped leader faint and only as far down as it has come, then the strokes; a flash on the ground it struck
  for (const Strike& S : strikes) {
    const float lead = S.restrike[0], t = S.t;
    const float first = t >= lead ? expf(-(t - lead) * 22.f) : 0.f, after = t >= lead ? 0.1f * expf(-(t - lead) * 5.f) : 0.f, all = S.flash(t);
    for (size_t i = 0; i < S.a.size(); i++) {
      float I;
      if (t < lead) { if (S.along[i] > t / lead) continue; I = 0.12f * S.k[i]; }
      else I = S.k[i] * (((int)i < S.main ? all : first) + after);
      if (I < 0.01f) continue;
      const vec3 m = (S.a[i] + S.b[i]) * 0.5f, d = (S.b[i] - S.a[i]) * 0.5f, vd = normalize(m - fp.camPos);
      vec3 side = cross(d, vd); const float sl = length(side); if (sl < 1e-4f) continue;
      const float w = std::max(S.w[i], length(m - fp.camPos) * 0.0022f);
      const float dl = length(d);
      quadAx(add, m, d * ((dl + w * 0.35f) / std::max(dl, 1e-3f)), side * (w / sl), vec3(0.85f, 0.88f, 1.f) * 7.f, I, SPR_BOLT, 6.f);
    }
    if (S.ground && t >= lead) bill(add, S.foot + vec3(0, 6.f, 0), 70.f, vec3(0.8f, 0.85f, 1.f) * 3.f, all, SPR_GLOW, 8.f);
  }
  // the wake on the water (updateWaterEffects): each ring flat on the surface, spreading and fading as it ages - drawn
  // under the particles, the spray over it
  for (const WakeRing& w : wakeRings) {
    const float ph = w.age / w.life;
    if (length(w.p - fp.camPos) > 1500.f) continue;
    if (w.ring) { const float r = w.size * (0.4f + 1.4f * ph); quadAx(alpha, w.p, vec3(r, 0, 0), vec3(0, 0, r), vec3(0.92f, 0.94f, 0.96f), 0.4f * w.a * (1.f - ph), SPR_SPLASH + std::min(ph, 0.99f) * 0.399f, 0.05f); }
    else {   // (a soft patch of foam, its pattern its own: a seed from where it was left)
      const float r = w.size * (1.f + 1.6f * sqrtf(ph)), seed = 0.399f * (0.5f + 0.5f * sinf(w.p.x * 1.7f + w.p.z * 2.3f));
      quadAx(alpha, w.p, vec3(r, 0, 0), vec3(0, 0, r), vec3(0.93f, 0.95f, 0.97f), 0.42f * w.a * (1.f - ph) * (1.f - ph), SPR_SMOKE + seed, 0.05f);
    }
  }
  // particles
  vec3 camVel = screen == SCR_FLIGHT && plane.spec ? plane.vel : vec3();
  std::vector<std::pair<float, const Particle*>> order;
  for (const Particle& p : particles) order.push_back({-length(p.p - fp.camPos), &p});
  std::sort(order.begin(), order.end(), [](auto& a, auto& b) { return a.first < b.first; });
  for (auto& o : order) {
    const Particle& p = *o.second;
    if (p.gate && photo.on && !photo.markers) continue;   // (photo mode: the gates' sparks go with the gates)
    // (its own billows: a seed from what it was born with, the same every frame of its life)
    const float seed = 0.4f * (0.5f + 0.5f * sinf(p.maxLife * 7919.f + p.grow * 104.7f + p.col.x * 31.f)) * 0.999f;
    float fade = clampf(p.life / p.maxLife, 0, 1);
    float a = p.alpha * (p.kind == SPR_FIRE || p.instant ? fade : fade * smoothstepf(0.f, 0.15f, 1.f - fade + 0.15f));
    if (p.kind == SPR_SPARK) {   // hot embers: streaks along their motion relative to the camera (1/60 s shutter)
      vec3 rv = p.v - camVel, vd = normalize(p.p - fp.camPos);
      vec3 perp = rv - vd * dot(rv, vd);
      float pl = length(perp), len = std::min(std::min(pl / 60.f * 0.6f, 2.5f), pl * (p.maxLife - p.life));   // never longer than its path since it left the nozzle
      vec3 ax = pl > 0.01f ? perp / pl : cr, ay = normalize(cross(ax, vd));
      quadAx(add, p.p - ax * (len * 0.5f), ax * (len * 0.5f + p.size), ay * p.size, p.col, fade, p.kind, 1.f);
    }
    else if (p.kind == SPR_FLAME) {   // billowing flame: white-hot yellow, then deep red, then soot (alpha-blended)
      float t = 1.f - fade;
      vec3 hot = p.col * 3.f, red = vec3(1.f, 0.28f, 0.06f) * 1.3f, soot = vec3(0.05f, 0.045f, 0.04f);
      vec3 col = t < 0.4f ? lerp(hot, red, t / 0.4f) : lerp(red, soot, std::min(1.f, (t - 0.4f) / 0.4f));
      float al = p.alpha * smoothstepf(0.f, 0.06f, 1.f - fade + 0.02f) * (t < 0.8f ? 1.f : 1.f - (t - 0.8f) / 0.2f);
      bill(alpha, p.p, std::max(p.size, 0.05f), col, al, p.kind + seed, 1.f);
    }
    else if (p.kind == SPR_FIRE) {   // flame cools from its spawn colour toward a deep red as it ages
      float cool = 1.f - fade;
      vec3 col = lerp(p.col, vec3(0.9f, 0.18f, 0.04f) * (p.col.x + p.col.y + p.col.z) * 0.4f, cool * cool);
      bill(add, p.p, std::max(p.size, 0.05f), col, a, p.kind + seed, 1.f);
    }
    else bill(alpha, p.p, p.size, p.col, a, p.kind + (p.kind == SPR_SMOKE ? seed : 0.f), 1.f);
  }
  // precipitation around the camera: as many drops as it is raining hard here (weather.cpp rainAt: heavier under
  // the denser cells, none above the tops), each streaked along its motion past the camera (its fall, the wind and
  // gusts that carry it, the camera's own speed: a 1/30 s shutter), some bigger and quicker than others; lit by
  // the sky, by lightning, and brightly where the landing lights' beams catch them
  const float precipK = wx.precip == 2 ? std::max(rainNow, 0.25f) : rainNow;
  if (screen == SCR_FLIGHT && wx.precip > 0 && precipK > 0.02f) {
    bool rain = wx.precip == 1;
    const int NMax = rain ? 1800 : 2000, N = (int)(NMax * (rain ? 0.15f + 0.85f * precipK : 0.4f + 0.6f * precipK));
    if (rainDrops.size() != (size_t)NMax) { rainDrops.resize(NMax); Rng r(5); for (auto& d : rainDrops) d = vec3(r.range(-30, 30), r.range(-20, 20), r.range(-30, 30)); }
    vec3 camV = screen == SCR_FLIGHT && plane.spec ? plane.vel : vec3();
    vec3 wv = plane.windVel;
    const bool lit = landingLight && plane.spec && plane.engineRunning && !crashed;
    const vec3 lp = plane.spec ? plane.pos + plane.forward() * (plane.spec->fusLen * 0.4f) : vec3(), ld = plane.spec ? normalize(plane.forward() - plane.up() * 0.1f) : vec3(0, 0, -1);
    const float nightK = smoothstepf(0.1f, -0.12f, fp.sunDir.y);
    for (int i = 0; i < N; i++) {
      vec3& d = rainDrops[i];
      float tt = realTime, big = (float)((i * 2654435761u) >> 24) / 255.f;   // (the drop's size: the bigger ones fall faster)
      vec3 fall = rain ? vec3(0, -(6.5f + 3.5f * big), 0) : vec3(0, -(0.9f + 0.6f * big), 0);
      vec3 rel = fall + wv - camV;
      vec3 p = d + rel * fmodf(tt * 0.6f + (i % 37) * 0.13f, 1.0f) * 0.12f * (rain ? 6.f : 1.f);
      if (!rain) p = p + vec3(sinf(tt * 1.3f + i), 0, cosf(tt * 1.1f + i * 0.7f)) * (0.5f + 0.06f * wx.windSpeed);
      // wrap into the box around the camera
      auto wrap = [](float v, float h) { return v - floorf((v + h) / (2 * h)) * 2 * h; };
      vec3 wpos(wrap(p.x, 30.f), wrap(p.y, 20.f), wrap(p.z, 30.f));
      const float dc = length(wpos);
      if (dc < 3.f) continue;   // (none inside the cabin, none smeared across the whole view)
      wpos = wpos + fp.camPos;
      vec3 col = rain ? vec3(0.75f, 0.78f, 0.85f) : vec3(1, 1, 1);
      if (lit) {   // in a landing light's beam: lit far above the night's ambient (col past 1 is light, SPR_RAIN / SPR_SNOW)
        vec3 tl = wpos - lp; float dl = length(tl), cs = dot(tl, ld) / std::max(dl, 0.1f);
        float k = smoothstepf(0.9f, 0.97f, cs) / (1.f + dl * dl / 900.f) * (0.35f + 0.65f * nightK);
        if (k > 0.01f) col = vec3(1.f) + vec3(1.f, 0.93f, 0.8f) * (2.2f * k);
      }
      if (lightning > 0.05f) col = vec3(1.f) + vec3(0.8f, 0.85f, 1.f) * (lightning * 0.6f) + (col.x > 1.f ? col - vec3(1.f) : vec3());
      if (rain) {   // (a 1/60 s streak, and the nearest shorter and fainter: a drop a few metres off crossed the whole view)
        vec3 dir = normalize(rel); float len = std::min(clampf(length(rel) * 0.018f, 0.25f, 2.f), dc * 0.12f);
        vec3 side = normalize(cross(dir, wpos - fp.camPos)) * (0.008f + 0.008f * big);
        quadAx(alpha, wpos, side, dir * len, col, (0.25f + 0.2f * precipK) * smoothstepf(3.f, 9.f, dc), SPR_RAIN, 0.5f);
      } else {
        bill(alpha, wpos, 0.07f + 0.05f * big, col, 0.9f, SPR_SNOW, 0.5f);
      }
    }
    if (rain) {
      vec3 col = vec3(0.75f, 0.78f, 0.85f);
      if (lightning > 0.05f) col = vec3(1.f) + vec3(0.8f, 0.85f, 1.f) * (lightning * 0.6f);
      // the rain further off (30 to 110 m): coarser, fainter streaks - a third of the near ones' number, each standing
      // for many drops - so that heavy rain has depth rather than ending a few metres from the eye
      const int NF = N / 3;
      for (int i = 0; i < NF; i++) {
        const vec3& d = rainDrops[i];
        const float big = (float)((i * 2246822519u) >> 24) / 255.f;
        const vec3 rel = vec3(0, -(7.5f + 2.5f * big), 0) + wv - camV;
        vec3 p = d * 3.7f + rel * fmodf(realTime * 0.4f + (i % 41) * 0.11f, 1.0f) * 1.2f;
        auto wrap = [](float v, float h) { return v - floorf((v + h) / (2 * h)) * 2 * h; };
        vec3 wpos(wrap(p.x, 110.f), wrap(p.y, 45.f), wrap(p.z, 110.f));
        const float dc = length(vec3(wpos.x, 0.f, wpos.z));
        if (dc < 30.f || dc > 110.f) continue;
        wpos = wpos + fp.camPos;
        const vec3 dir = normalize(rel), side = normalize(cross(dir, wpos - fp.camPos)) * (0.04f + 0.03f * big);
        quadAx(alpha, wpos, side, dir * (2.5f + 2.f * big), col, (0.05f + 0.06f * precipK) * smoothstepf(110.f, 70.f, dc), SPR_RAIN, 2.f);
      }
      // splashes where it lands round the camera while it is low: a ring spreading on the ground or the water and, on the
      // ground, a crown of droplets thrown up - each spot's own tenth of a second, then on to another spot
      const float camGround = std::max(g_world.height(fp.camPos.x, fp.camPos.z), 0.f), camAgl = fp.camPos.y - camGround;
      if (camAgl < 60.f) {
        const int NS = (int)(260.f * precipK * (1.f - camAgl / 60.f));
        for (int i = 0; i < NS; i++) {
          const float cyc = realTime * 9.f + (float)((i * 2654435761u) >> 8) / 16777216.f;
          const float ph = cyc - floorf(cyc);
          Rng rr((uint32_t)i * 73856093u ^ (uint32_t)(int)floorf(cyc) * 19349663u ^ 0x9e3779b9u);
          vec3 at = fp.camPos + vec3(rr.range(-1.f, 1.f), 0.f, rr.range(-1.f, 1.f)) * (3.f + 22.f * rr.uni());
          const float g = g_world.height(at.x, at.z);
          const bool water = g < 0.3f;
          at.y = water ? 0.03f : g + 0.04f;
          const float dc = length(at - fp.camPos);
          if (dc < 1.5f) continue;
          const float sz = 0.06f + 0.22f * ph + 0.05f * rr.uni();
          quadAx(alpha, at, vec3(sz, 0, 0), vec3(0, 0, sz), col, (water ? 0.55f : 0.4f) * smoothstepf(28.f, 12.f, dc), SPR_SPLASH + ph * 0.399f, 0.05f);
          if (!water && ph < 0.55f && dc < 12.f) bill(alpha, at + vec3(0, 0.06f, 0), 0.09f, col, 0.35f * smoothstepf(12.f, 5.f, dc), 14.f + ph * 0.399f, 0.05f);   // (the crowns only close by: further off a few pixels' chevron)
        }
      }
    }
  }
}
