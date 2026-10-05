// Real shared-model flights for the optional candidate roster; no forced landing or state rescue.
#include "candidates.h"
#include "career.h"
#include "aero.h"
#include <array>
#include <filesystem>
#include <fstream>

static float sq(float x) { return x*x; }

static bool finite(const Plane& p) {
  return std::isfinite(p.pos.x + p.pos.y + p.pos.z + p.vel.x + p.vel.y + p.vel.z +
                       p.w.x + p.w.y + p.w.z + p.q.w + p.q.x + p.q.y + p.q.z + p.fuel + p.gLoad);
}

// Mirror the model's monotone station interpolation for the guide's cabin-clearance check.
static vec3 section(const ModelDef& m, float z) {
  z = clampf(z, m.st[0][0], m.st[7][0]); int i = 0;
  for (int k = 0; k < 7; k++) { i = k; if (z <= m.st[k + 1][0]) break; }
  const float* a = m.st[i]; const float* b = m.st[i + 1];
  const float h = b[0] - a[0], t = (z - a[0])/h, t2 = t*t, t3 = t2*t;
  auto slope = [](float d0, float d1, float h0, float h1) {
    return d0*d1 <= 0 ? 0.f : 3.f*(h0 + h1)/((2.f*h1 + h0)/d0 + (h1 + 2.f*h0)/d1);
  };
  float v[3];
  for (int c = 1; c <= 3; c++) {
    float d = (b[c] - a[c])/h, ma = d*.5f, mb = d*.5f;
    if (i) { float h0 = a[0] - m.st[i - 1][0]; ma = slope((a[c] - m.st[i - 1][c])/h0, d, h0, h); }
    if (i < 6) { float h1 = m.st[i + 2][0] - b[0]; mb = slope(d, (m.st[i + 2][c] - b[c])/h1, h, h1); }
    v[c - 1] = a[c]*(2*t3 - 3*t2 + 1) + ma*h*(t3 - 2*t2 + t) + b[c]*(-2*t3 + 3*t2) + mb*h*(t3 - t2);
  }
  return vec3(v[0], v[1], v[2]);
}

static float guidePayload(const AircraftSpec& s) { return s.cargoKg + 90.f*s.pax + 90.f; }
static bool guideChecks(int index) {
  const auto& s = kAircraft[index]; const auto& m = kModels[index];
  const float mass = s.emptyMass + s.maxFuel + guidePayload(s), weight = mass*9.81f;
  const float vs1 = sqrtf(2*weight/(1.225f*s.wingArea*s.CLmax));
  const float vs0 = sqrtf(2*weight/(1.225f*s.wingArea*(s.CLmax + s.flapCL)));
  const float ix = .12f*mass*sq(s.span*.5f), iy = .18f*mass*sq(s.fusLen*.5f);
  auto close = [](float a, float b, float fraction) { return fabsf(a - b) <= b*fraction; };
  vec3 eyeSec = section(m, m.eye.z);
  const float roof = eyeSec.z + (eyeSec.y - .035f)*sqrtf(std::max(0.f, 1.f - sq(m.eye.x/(eyeSec.x - .035f))));
  const float sparGap = m.eye.y - 1.08f - (m.wing[4] + m.wing[1]*m.wing[7]*.5f);
  float maxH = 0; for (int i = 0; i < 8; i++) maxH = std::max(maxH, m.st[i][2]);
  const float track = std::max(1.2f, .13f*s.span), k = track/m.wing[0];
  const float le = m.wing[5] + m.wing[3]*k, te = le + m.wing[1] + (m.wing[2] - m.wing[1])*k;
  const float mz = .04f*s.fusLen;
  bool ok = s.special == 0 && m.engine <= 4 && m.gear <= 4 && s.rangeKm >= 40 && s.rangeKm <= 300 &&
            weight/s.wingArea >= 400 && weight/s.wingArea <= (s.engineType == ENG_JET ? 4000 : 1200) &&
            s.vr >= vs0*1.10f && s.vr <= vs0*1.15f && close(s.vref, vs0*1.3f, .03f) &&
            s.vref > s.vr && s.cruise >= 1.6f*s.vref &&
            close(s.Ixx, ix, .4f) && close(s.Iyy, iy, .4f) && close(s.Izz, ix + iy, .4f) &&
            close(s.fusLen, m.st[7][0] - m.st[0][0], .1f) && close(s.fusRad, maxH, .1f) &&
            m.wing[7] >= .10f && m.wing[7] <= .16f && sparGap > 0 && roof - m.eye.y >= .08f &&
            m.wsZ0 < m.wsZ1 && m.wsZ1 < m.eye.z - (m.cockpit == 2 ? .85f : .68f) &&
            m.wsY < section(m, m.wsZ0).z + section(m, m.wsZ0).y &&
            m.st[0][1] <= .1f && m.st[0][2] <= .1f && m.st[7][1] <= .1f && m.st[7][2] <= .1f &&
            mz - m.wheelR - .08f > le && mz + m.wheelR + .08f < te;
  for (int i : {2, 3, 4}) ok &= m.st[i][2] >= m.st[i][1]*.5f;
  const float loading = s.engineType == ENG_JET ? s.power*s.engines/weight : mass/(s.power*.001f*s.engines);
  ok &= s.engineType == ENG_JET ? loading >= .25f && loading <= .40f : loading >= 5 && loading <= 9;
  printf("guide %s: %s; max mass %.0f kg, loading %.2f N/m2, stalls %.3f/%.3f m/s, rotate ratio %.3f, power/thrust loading %.3f, inertia reference %.1f/%.1f/%.1f; roof clearance %.3f m, spar clearance %.3f m, wheel well %.3f..%.3f within wing %.3f..%.3f\n",
         s.id, ok ? "PASS" : "FAIL", mass, weight/s.wingArea, vs1, vs0, s.vr/vs0, loading, ix, iy, ix + iy,
         roof - m.eye.y, sparGap, mz - m.wheelR - .08f, mz + m.wheelR + .08f, le, te);
  return ok;
}

static bool geometry(int index) {
  const auto& s = kAircraft[index]; const auto& m = kModels[index];
  bool ok = fabsf(s.span - m.wing[0] * 2) < .001f &&
            fabsf(s.wingArea - m.wing[0] * (m.wing[1] + m.wing[2])) < .001f &&
            fabsf(s.wingY * s.fusRad - m.wing[4]) < .001f;
  for (int i = 0; i < 8; i++) {
    ok &= m.st[i][1] > 0 && m.st[i][2] > 0;
    if (i) ok &= m.st[i][0] > m.st[i - 1][0];
  }
  const float lam = m.wing[2] / m.wing[1];
  const float mac = (2.f / 3.f) * m.wing[1] * (1 + lam + lam * lam) / (1 + lam);
  ok &= fabsf(mac - s.chord) < .002f;
  Plane p; p.reset(&s, vec3(0, 1500, 0), 0, s.maxFuel * .6f, 100, true, s.cruise);
  float packed[96]; packModel(s, index, p.gearHeight(), packed);
  for (float v : packed) ok &= std::isfinite(v);
  float hubs[2][4]; const int props = modelProps(m, hubs);
  ok &= props == (s.engineType == ENG_JET ? 0 : s.engines);
  for (int k = 0; k < props; k++) ok &= hubs[k][1] - hubs[k][3] + p.gearHeight() >= .25f;
  const auto& a = aeroModel(s);
  for (float V : {15.f, s.vref, s.cruise, s.cruise * 1.4f}) {
    const float cd = aeroCD0(a, s, V, 1.225f, V / 340.f);
    ok &= std::isfinite(cd) && cd > 0 && cd < .3f;
  }
  const auto& P = Plane::perf(&s);
  ok &= P.roc > .5f && P.vs0 > 10 && P.vs1 > P.vs0 && P.rollRate > .1f && P.tQ > 0;
  printf("geometry/performance %s: %s, clean stall %.2f, landing stall %.2f, best climb %.2f m/s at %.2f m/s\n",
         s.id, ok ? "PASS" : "FAIL", P.vs1, P.vs0, P.roc, P.vy);
  return ok;
}

int main(int argc, char** argv) {
  setvbuf(stdout, nullptr, _IONBF, 0);
  const char* path = argc > 1 ? argv[1] : "candidate-flights.csv";
  FILE* csv = fopen(path, "w"); if (!csv) return 2;
  fprintf(csv, "aircraft,case,airport,wind,start,payload_kg,result,seconds,touchdown_mps,along_m,cross_m,min_fuel,max_g,reason\n");
  g_world.build(); buildStory();
  const bool groundOnly = argc > 2 && std::string(argv[2]) == "--ground-only";
  const bool trace = getenv("TRACE_TAKEOFF") != nullptr;
  bool all = true; int cases = 0, passed = 0;
  for (const auto& entry : candidate::entries) {
    const auto& s = kAircraft[entry.index];
    if (!groundOnly) { all &= guideChecks(entry.index); all &= geometry(entry.index); }
    if (!groundOnly) {
      Weather calm; calm.windSpeed = calm.gust = calm.turbulence = 0;
      // Each control must turn the aeroplane in the same sense as the animated surface.
      for (int axis = 0; axis < 3; axis++) {
        const float speed = std::max(s.vref*1.4f, std::min(s.cruise*.6f, s.vref*2.f));
        Plane p; p.reset(&s, vec3(0, 1500, 0), 90, s.maxFuel*.5f, 0, true, speed);
        const float b0 = p.bankDeg(), pt0 = p.pitchDeg(), h0 = p.heading();
        for (int k = 0; k < 120 && !p.ev.crashed; k++) {
          p.ctl.roll = axis == 0 ? 1.f : 0.f; p.ctl.pitch = axis == 1 ? .6f : 0.f; p.ctl.yaw = axis == 2 ? 1.f : 0.f;
          p.step(1.f/120.f, calm, k/120.f);
        }
        const float d = axis == 0 ? p.bankDeg() - b0 : axis == 1 ? p.pitchDeg() - pt0 : wrapAngle((p.heading() - h0)*DEG)/DEG;
        const bool ok = finite(p) && !p.ev.crashed && d > (axis == 2 ? .3f : 1.f);
        printf("control %s axis%d: %s, %.3f degrees\n", s.id, axis, ok ? "PASS" : "FAIL", d);
        fprintf(csv, "%s,control%d,,0,0,0,%s,1,0,0,0,%.2f,%.3f,%s\n", s.id, axis, ok ? "pass" : "fail", p.fuel, p.gLoad, ok ? "" : "wrong control sense");
        cases++; passed += ok; all &= ok;
      }
      Plane p; p.reset(&s, vec3(0, 1800, 2000), 30, s.maxFuel*.6f, 100, true, s.cruise*.85f);
      p.apComfort = true; p.apEngage(Plane::AP_HOLD, -1, calm);
      float maxBank = 0, maxG = 1, minG = 1;
      for (int k = 0; k < 240*60 && !p.ev.crashed; k++) {
        if (k == 600) { p.apHeading = wrapDeg360(p.apHeading + 150.f); p.apAlt += 150.f; }
        p.step(1.f/60.f, calm, k/60.f);
        if (k > 600) { maxBank = std::max(maxBank, fabsf(p.bankDeg())); maxG = std::max(maxG, p.gLoad); minG = std::min(minG, p.gLoad); }
      }
      const float he = fabsf(wrapAngle((p.apHeading - p.heading())*DEG)/DEG), ae = fabsf(p.apAlt - p.pos.y);
      bool ok = finite(p) && !p.ev.crashed && he < 3 && ae < 20 && maxBank <= 26 && maxG <= 1.3f && minG >= .8f;
      printf("comfort hold %s: %s, heading error %.2f deg, altitude error %.2f m, bank %.2f deg, g %.3f..%.3f\n", s.id, ok ? "PASS" : "FAIL", he, ae, maxBank, minG, maxG);
      fprintf(csv, "%s,comfort-hold,,0,0,100,%s,240,0,%.2f,%.2f,%.2f,%.3f,%s\n", s.id, ok ? "pass" : "fail", ae, he, p.fuel, maxG, ok ? "" : "hold/comfort limits");
      cases++; passed += ok; all &= ok;
      const int ai = g_world.findAirport("CAP"); const auto& A = g_world.airports[ai];
      vec3 side(-A.dir().z, 0, A.dir().x), start = A.pos() + side*14000.f + A.dir()*3000.f;
      start.y = std::max(A.elev + 1200.f, g_world.height(start.x, start.z) + 500.f);
      Plane q; q.reset(&s, start, wrapDeg360(A.heading + 120.f), s.maxFuel, 100, true, s.cruise*.85f);
      q.apComfort = true; q.apEngage(Plane::AP_NAV, ai, calm);
      maxBank = 0; maxG = minG = 1; int ticks = 0; bool touchdown = false; float tdVs = 0;
      for (; ticks < 1500*60 && !q.ev.crashed && !q.apDone; ticks++) {
        q.step(1.f/60.f, calm, ticks/60.f);
        if (q.ev.touchdown && !touchdown) { touchdown = true; tdVs = -q.ev.touchdownVs; }
        if (ticks > 300 && q.apStage == Plane::APS_NAV) { maxBank = std::max(maxBank, fabsf(q.bankDeg())); maxG = std::max(maxG, q.gLoad); minG = std::min(minG, q.gLoad); }
      }
      vec3 rel = q.pos - A.pos(); float along = fabsf(dot(rel, A.dir())), cross = fabsf(dot(rel, side));
      ok = finite(q) && !q.ev.crashed && q.apDone && touchdown && tdVs < 3 && along < A.length*.5f && cross < A.width*.5f &&
           q.fuel > 0 && maxBank <= 26 && maxG <= 1.3f && minG >= .8f;
      printf("comfort route %s: %s, %.1fs, touchdown %.2f m/s, bank %.2f deg, g %.3f..%.3f\n", s.id, ok ? "PASS" : "FAIL", ticks/60.f, tdVs, maxBank, minG, maxG);
      fprintf(csv, "%s,comfort-route,CAP,0,0,100,%s,%.3f,%.4f,%.2f,%.2f,%.2f,%.3f,%s\n", s.id, ok ? "pass" : "fail", ticks/60.f, tdVs, along, cross, q.fuel, maxG, ok ? "" : "landing/comfort limits");
      cases++; passed += ok; all &= ok;
    }
    // Identical familiar fields, with the normal runway eligibility predicate.
    if (!groundOnly) for (const char* code : {"PVI", "CAP", "NPT", "CDR"}) {
      const int ai = g_world.findAirport(code); if (ai < 0) { all = false; continue; }
      const auto& A = g_world.airports[ai];
      if (!runwayOK(s, A)) continue;
      for (int wind = 0; wind < 3; wind++) for (int start = 0; start < 2; start++) for (int load = 0; load < 2; load++) {
        Weather wx; wx.windSpeed = wind == 0 ? .5f : wind == 1 ? 5.f : 8.f;
        wx.windFrom = wrapDeg360(A.heading + (wind == 2 ? 120.f : 70.f));
        wx.gust = wind == 2 ? 4.f : 0.f; wx.turbulence = wind == 0 ? .05f : wind == 1 ? .2f : .45f;
        const float payload = load ? guidePayload(s) : std::min(s.cargoKg * .25f, 150.f);
        vec3 dir = start ? -A.dir() : vec3(sinf(35.f * DEG), 0, -cosf(35.f * DEG));
        vec3 pos = A.pos() + dir * (start ? 7000.f : 14000.f);
        pos.y = std::max(A.elev + (start ? 550.f : 1200.f), g_world.height(pos.x, pos.z) + 500.f);
        Plane p; p.reset(&s, pos, start ? A.heading : wrapDeg360(A.heading + 95.f), s.maxFuel * .7f, payload, true, s.cruise * .8f);
        p.apComfort = false; p.apEngage(Plane::AP_NAV, ai, wx);
        bool numeric = true, touchdown = false; float tdVs = 0, minFuel = p.fuel, maxG = 1; int ticks = 0;
        for (; ticks < 1800 * 60 && !p.ev.crashed && !p.apDone; ticks++) {
          p.step(1.f / 60.f, wx, ticks / 60.f);
          numeric &= finite(p); if (!numeric) break;
          minFuel = std::min(minFuel, p.fuel); maxG = std::max(maxG, p.gLoad);
          if (p.ev.touchdown && !touchdown) { touchdown = true; tdVs = -p.ev.touchdownVs; }
        }
        vec3 rel = p.pos - A.pos(); const float along = fabsf(dot(rel, A.dir()));
        const float cross = fabsf(dot(rel, vec3(-A.dir().z, 0, A.dir().x)));
        const bool ok = numeric && !p.ev.crashed && p.apDone && touchdown && tdVs < 3.f &&
                        along < A.length * .5f && cross < A.width * .5f && minFuel > 0;
        const char* why = !numeric ? "non-finite" : p.ev.crashed ? p.ev.crashReason.c_str() : !p.apDone ? "timeout" :
                          !touchdown ? "no touchdown" : tdVs >= 3 ? "hard touchdown" : minFuel <= 0 ? "fuel" : "off runway";
        fprintf(csv, "%s,autoland,%s,%d,%d,%.0f,%s,%.3f,%.4f,%.2f,%.2f,%.2f,%.3f,%s\n", s.id, A.code, wind, start,
                payload, ok ? "pass" : "fail", ticks / 60.f, tdVs, along, cross, minFuel, maxG, ok ? "" : why);
        fflush(csv); printf("%s %s wind%d start%d load%d: %s %.1fs touchdown %.2f\n", s.id, A.code, wind, start, load,
                             ok ? "PASS" : why, ticks / 60.f, tdVs);
        cases++; passed += ok; all &= ok;
      }
    }
    // Full-power ground departure: use controls, never reset to an airborne state.
    for (int load = 0; load < 2; load++) {
      const int ai = g_world.findAirport("CAP"); const auto& A = g_world.airports[ai];
      const float payload = load ? guidePayload(s) : std::min(s.cargoKg * .25f, 150.f);
      Weather wx; wx.windSpeed = .5f; wx.gust = 0; wx.turbulence = .02f;
      Plane p; p.reset(&s, A.pos() - A.dir() * (A.length * .5f - 80.f), A.heading, s.maxFuel, payload, false);
      p.engineRunning = true; p.engineSpool = 0; p.ctl.brake = 0; p.ctl.throttle = 1; p.ctl.flaps = .3f;
      bool numeric = true, lifted = false; int ticks = 0; float maxG = 1, liftoffDistance = -1;
      for (; ticks < 240 * 60 && !p.ev.crashed; ticks++) {
        // Normal rotation and climb attitude through controls, with pitch-rate damping.
        p.ctl.pitch = p.ias > s.vr ? clampf((10.f - p.pitchDeg()) * .08f - p.w.x * .5f, -1.f, 1.f) : 0;
        p.ctl.roll = clampf(-p.bankDeg() * .05f + p.w.z * .3f, -1.f, 1.f);
        p.ctl.yaw = clampf(wrapAngle((A.heading - p.heading()) * DEG) * 3.f, -1.f, 1.f);
        p.step(1.f / 60.f, wx, ticks / 60.f); numeric &= finite(p); if (!numeric) break;
        maxG = std::max(maxG, p.gLoad);
        if (liftoffDistance < 0 && p.agl() > 2 && !p.onGround)
          liftoffDistance = dot(p.pos - A.pos(), A.dir()) + A.length*.5f - 80.f;
        if (trace && ticks % 15 == 0) printf("TRACE %s load%d t=%.2f ias=%.2f agl=%.2f ground=%d pitch=%.2f q=%.3f bank=%.2f alpha=%.2f ctlP=%.3f vs=%.2f tailStrike=%d crash=%s\n", s.id, load, ticks/60.f, p.ias, p.agl(), int(p.onGround), p.pitchDeg(), p.w.x, p.bankDeg(), p.alpha/DEG, p.ctl.pitch, p.vel.y, int(p.ev.tailStrike), p.ev.crashReason.c_str());
        if (p.agl() > 25.f && !p.onGround) { lifted = true; break; }
      }
      const float along = dot(p.pos - A.pos(), A.dir()) + A.length * .5f - 80.f;
      const bool ok = numeric && lifted && !p.ev.crashed && p.fuel > 0 && along < A.length - 100.f &&
                      liftoffDistance > 0 && liftoffDistance < s.runwayM*.9f;
      const char* why = !numeric ? "non-finite" : p.ev.crashed ? p.ev.crashReason.c_str() : !lifted ? "no liftoff" : "runway overrun";
      fprintf(csv, "%s,takeoff,%s,0,0,%.0f,%s,%.3f,0,%.2f,0,%.2f,%.3f,%s\n", s.id, A.code, payload,
              ok ? "pass" : "fail", ticks / 60.f, along, p.fuel, maxG, ok ? "" : why);
      printf("%s full-power takeoff load%d: %s; liftoff %.1fm (limit %.1f), 25m AGL at %.0fm, %.1fs\n", s.id, load, ok ? "PASS" : why, liftoffDistance, s.runwayM*.9f, along, ticks / 60.f);
      cases++; passed += ok; all &= ok;
    }
  }
  fclose(csv); printf("candidate flight checks: %d/%d passed\n", passed, cases);
  return all ? 0 : 1;
}
