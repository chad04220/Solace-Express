// Breakup: every type comes apart into its own components, each surface goes with the piece it belongs to, the pieces
// weigh what the airframe did, and they fly down on their own air - wings tumbling and drifting, the heavy fuselage
// falling fast, skin panels fluttering - without the integration ever blowing up.
#include "../src/breakup.h"
#include "../src/aircraft.h"
#include "../src/models.h"
#include "../src/aero.h"
#include "../src/gear_breakup_geometry.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <chrono>

static bool finite3(vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
static float gearHeightFor(const AircraftSpec& s) { Plane p; p.reset(&s, vec3(0, 100, 0), 0, s.maxFuel * 0.5f, 0, true, s.cruise); return p.gearHeight(); }

int main() {
  setvbuf(stdout, nullptr, _IONBF, 0);
  int failures = 0;
  for (int t = 0; t < kAircraftCount; t++) {
    const AircraftSpec& s = kAircraft[t];
    const ModelDef& m = kModels[t];
    const float gh = gearHeightFor(s);
    // Every extended assembly has one debris body. Test real wheel stations (the
    // old regression checked half of the actual track and missed attached wheels).
    for (float gear : {0.f, .2f, .21f, .3f, .65f, 1.f}) {
      BreakPiece gp[kMaxBreakPieces];
      const int gn = breakPieces(s, gear, gh, gp);
      const bool fixed = !s.special && m.gear <= 2, separate = fixed || gear > .2f;
      int owners[3] = {-1,-1,-1}, ng = 0, wings = 0;
      for (int k=0;k<gn;++k) {
        wings += gp[k].kind == BK_WING;
        if (gp[k].kind != BK_GEAR) continue;
        const int slot = gp[k].side < 0 ? 0 : gp[k].side > 0 ? 1 : 2;
        assert(owners[slot] < 0); owners[slot]=k; ++ng;
        assert(gp[k].rigidOnly);
        if(gp[k].rigidOnly) assert(breakOwner(gp,gn,gp[k].C) != k);
        BreakCut cut[kMaxBreakCuts]; assert(breakCuts(s,gp,gn,k,cut)>0);
      }
      assert(ng == (separate ? 3 : 0));
      assert(gn<=kMaxBreakPieces && wings==2 && gp[gn-1].kind==BK_CENTRE);
      if(t==kLarkspur || t==kAtlas) {
        int kinds[BK_COUNT]={};
        for(int j=0;j<gn;++j) {
          ++kinds[gp[j].kind];
          BreakCut cuts[kMaxBreakCuts];assert(breakCuts(s,gp,gn,j,cuts)>0);
        }
        assert(kinds[BK_WING]==2 && kinds[BK_TAIL]==2 && kinds[BK_FIN]==1);
        assert(kinds[BK_NOSE]==1 && kinds[BK_AFT]==1 && kinds[BK_CENTRE]==1);
        assert(kinds[BK_PROP]==(t==kLarkspur?1:0) && kinds[BK_NACELLE]==(t==kAtlas?2:0));
        assert(gn==(t==kLarkspur?12:separate?13:10));
        if(t==kAtlas) for(int side:{-1,1}) {
          // Both real fan assemblies (including their spinner noses) and the
          // aft core cone belong to their complete, separate nacelle body.
          // The old legacy +.12 m end bound left the Atlas +.42 m tip behind.
          for(float z:{m.nacZ0+.74f-.32f,m.nacZ0+.74f,m.nacZ0+m.nacLen+.42f}) {
            const int owner=breakOwner(gp,gn,vec3(side*m.nacX,m.nacY,z));
            assert(gp[owner].kind==BK_NACELLE && gp[owner].side==side);
          }
          for(int angle=0;angle<360;angle+=15) {
            const float a=angle*DEG;
            const vec3 rim(side*m.nacX+m.nacR*.90f*cosf(a),m.nacY+m.nacR*.90f*sinf(a),m.nacZ0+.74f);
            const int owner=breakOwner(gp,gn,rim);
            assert(gp[owner].kind==BK_NACELLE && gp[owner].side==side);
          }
        }
      }
      if (fixed) {
        const GearStations gs = gearStations(s);
        for (int sd : {-1,1}) for (float lateral : {-.06f,0.f,.06f}) for (float dy : {-.70f,0.f,.50f}) {
          const vec3 wheel(sd*(gs.track+lateral),m.wheelR-gh+dy*m.wheelR,gs.mainZ);
          const int own=owners[sd<0?0:1];
          const vec3 d=wheel-gp[own].C;
          assert(fabsf(d.x)<=gp[own].H.x && fabsf(d.y)<=gp[own].H.y && fabsf(d.z)<=gp[own].H.z);
          assert(breakOwner(gp,gn,wheel)!=own); // no static skin is copied into the gear piece

        }
      }
      if(separate) {
        DebrisBody bodies[kMaxBreakPieces];
        breakBodies(s,gp,gn,s.emptyMass,0,bodies);
        for(int owner:owners) {
          assert(owner>=0 && bodies[owner].mass>0 && finite3(bodies[owner].cg));
          for(int corner=0;corner<8;++corner) {
            const auto& body=bodies[owner];
            const vec3 p((corner&1)?body.hi.x:body.lo.x,(corner&2)?body.hi.y:body.lo.y,(corner&4)?body.hi.z:body.lo.z);
            assert(body.size+.001f>=2.f*length(p-body.cg));
          }
          vec3 p=bodies[owner].cg+vec3(0,50,0),v(20,0,0),w(0,0,1);quat q;
          debrisStep(bodies[owner],p,v,q,w,vec3(0),1.225f,.1f);
          assert(finite3(p)&&finite3(v)&&finite3(w));
        }
        const GearStations gs=gearStations(s);
        const auto pose=gearBreakup::mainPose(s,m,gs,gh,gear);
        const float radius=s.special?.38f:m.wheelR;
        const vec3 wheel(gs.track,radius-gh,gs.mainZ);
        auto posed=[&](vec3 p){return pose.hinge+pose.rotation.rotate(p-pose.hinge);};
        auto inside=[&](int side,vec3 point){
          point.x*=side;const DebrisBody& b=bodies[owners[side<0?0:1]];
          assert(point.x>=b.lo.x-.001f&&point.x<=b.hi.x+.001f&&point.y>=b.lo.y-.001f&&point.y<=b.hi.y+.001f&&point.z>=b.lo.z-.001f&&point.z<=b.hi.z+.001f);
        };
        for(int side:{-1,1}) {
          inside(side,pose.hinge);inside(side,posed(wheel));
          const vec3 knee(gs.track,-2.40f,2.72f);
          if(t==kAtlas) inside(side,posed(knee));
          vec3 expected=posed(lerp(pose.hinge,wheel,.68f)+(t==kAtlas?(knee-pose.hinge)*.15f:vec3(0)));expected.x*=side;
          assert(length(bodies[owners[side<0?0:1]].cg-expected)<.001f);
          for(float axle:{-1.f,1.f})for(float tyre:{-1.f,1.f})for(int angle=0;angle<360;angle+=15)for(float shoulder:{-1.f,1.f}) {
            const bool atlas=t==kAtlas;
            const float x=atlas?tyre*.34f:!s.special&&m.gear==3?tyre*.22f:0.f;
            const float z=atlas?axle*.62f:0.f;
            const float half=atlas?.189f:!s.special&&m.gear==3?.154f:fixed?(m.gear==0?.065f:m.gear==1?.134f:.184f):.144f;
            inside(side,posed(wheel+vec3(x+shoulder*half,cosf(angle*DEG)*radius,z+sinf(angle*DEG)*radius)));
          }
        }
        if(t==kAtlas) { // independent matrix witness, including the yaw/roll order
          const float up=clampf((1-gear)*1.25f,0,1);
          const vec3 hinge(gs.track,-1.30f,2.72f),d=wheel-hinge;
          const float dx=1.85f-gs.track,dz=-sqrtf(d.y*d.y+d.z*d.z-dx*dx);
          const float a=-.5f*PI*smoothstepf(.25f,1.f,up);
          const float b=atan2f(d.y*dz-d.z*dx,d.y*dx+d.z*dz)*smoothstepf(0,.65f,up);
          const vec3 roll(cosf(a)*d.x-sinf(a)*d.y,sinf(a)*d.x+cosf(a)*d.y,d.z);
          const vec3 actual=hinge+vec3(cosf(b)*roll.x-sinf(b)*roll.z,roll.y,sinf(b)*roll.x+cosf(b)*roll.z);
          inside(1,actual);assert(length(actual-posed(wheel))<.001f);
        }
        if(s.taildragger) {
          const DebrisBody& tail=bodies[owners[2]];
          // The rendered tail wheel uses the model offset, not the calibrated
          // contact-station tailY used to settle a complete aircraft on a runway.
          const float bottom=-gh+.11f*s.fusLen;
          assert(bottom>=tail.lo.y-.001f && bottom+.2f<=tail.hi.y+.001f);
        }
      }
    }
    BreakPiece P[kMaxBreakPieces];
    const int n = breakPieces(s, 0.f, gh, P);
    assert(n >= 5 && n <= kMaxBreakPieces && P[n - 1].kind == BK_CENTRE);
    int count[BK_COUNT] = {};
    for (int i = 0; i < n; i++) { count[P[i].kind]++; assert(P[i].H.x > 0 && P[i].H.y > 0 && P[i].H.z > 0); }
    printf("%-16s %2d pieces:", s.name, n);
    for (int i = 0; i < n; i++) printf(" %s%s", P[i].side < 0 ? "L " : P[i].side > 0 ? "R " : "", breakKindName(P[i].kind));
    printf("\n");
    // each surface goes with its own piece
    const AeroGeom& g = aeroGeom(s);
    if (!s.special) {
      for (int i = 0; i < g.nSt; i++) {
        const AeroStrip& st = g.st[i];
        float hw, hh, cy; modelSection(m, st.r.z, hw, hh, cy);
        const int o = breakOwner(P, n, st.r);
        const bool outboard = fabsf(st.r.x) > hw + 0.15f;
        if (st.surf == AS_WING && outboard && P[o].kind != BK_WING && P[o].kind != BK_NACELLE && P[o].kind != BK_PROP) { printf("  wing strip %d at %.1f,%.1f,%.1f went to the %s\n", i, st.r.x, st.r.y, st.r.z, breakKindName(P[o].kind)); failures++; }
        if (st.surf == AS_WING && outboard && P[o].kind == BK_WING && (P[o].side > 0) != (st.r.x > 0)) { printf("  wing strip %d on the wrong wing\n", i); failures++; }
        if ((st.surf == AS_TAIL || st.surf == AS_CANARD) && outboard && P[o].kind != (st.surf == AS_TAIL ? BK_TAIL : BK_CANARD) && P[o].kind != BK_FIN) { printf("  tail strip %d at %.1f,%.1f,%.1f went to the %s\n", i, st.r.x, st.r.y, st.r.z, breakKindName(P[o].kind)); failures++; }
        if (st.surf == AS_FIN && st.r.y > cy + hh + 0.3f && P[o].kind != BK_FIN && P[o].kind != BK_TAIL) { printf("  fin strip %d at %.1f,%.1f,%.1f went to the %s\n", i, st.r.x, st.r.y, st.r.z, breakKindName(P[o].kind)); failures++; }
      }
      float pr[2][4]; const int np = modelProps(m, pr);
      for (int i = 0; i < np; i++) assert(P[breakOwner(P, n, vec3(pr[i][0], pr[i][1], pr[i][2]))].kind == BK_PROP);
      // the tips, the fin top and the tail cone
      assert(P[breakOwner(P, n, modelWingTip(m))].kind == BK_WING);
      const int ft = breakOwner(P, n, modelFinTop(m)); assert(P[ft].kind == BK_FIN || P[ft].kind == BK_TAIL);
      const int tc = breakOwner(P, n, modelTailTip(m)); assert(P[tc].kind == BK_AFT || P[tc].kind == BK_FIN);
    }
    // every piece tore from something: a wing at its root, the nose and the tail cone round the fuselage, the centre
    // section wherever the others left it
    printf("  tears:");
    for (int i = 0; i < n; i++) {
      BreakCut c[kMaxBreakCuts]; const int nc = breakCuts(s, P, n, i, c);
      printf(" %d", nc);
      for (int k = 0; k < nc; k++) assert(c[k].H.x > 0.f && c[k].H.y > 0.f && c[k].H.z > 0.f && finite3(c[k].C));
      const int kd = P[i].kind;
      const bool must = kd == BK_WING || kd == BK_NOSE || kd == BK_AFT || kd == BK_CENTRE || kd == BK_FIN || kd == BK_TAIL || kd == BK_GEAR || kd == BK_PROP;
      if (must && nc == 0) { printf("\n  the %s%s tore from nothing\n", P[i].side < 0 ? "left " : P[i].side > 0 ? "right " : "", breakKindName(kd)); failures++; }
      if (kd == BK_CENTRE && nc < 2) { printf("\n  the centre section tore in only %d place\n", nc); failures++; }
    }
    printf("\n");
    // the bodies weigh what the airframe did
    const float mass = s.emptyMass + s.cargoKg * 0.5f, payload = s.cargoKg * 0.5f;
    DebrisBody B[kMaxBreakPieces];
    breakBodies(s, P, n, mass, payload, B);
    float sum = 0;
    for (int i = 0; i < n; i++) {
      assert(B[i].mass > 0 && B[i].I.x > 0 && B[i].I.y > 0 && B[i].I.z > 0 && finite3(B[i].cg)); sum += B[i].mass;
      // its contact box holds its centre of mass and is no bigger than the airframe
      const vec3 lo = B[i].lo, hi = B[i].hi;
      assert(hi.x > lo.x && hi.y > lo.y && hi.z > lo.z);
      if(P[i].rigidOnly)
        for(int corner=0;corner<8;++corner) {
          const vec3 point((corner&1)?hi.x:lo.x,(corner&2)?hi.y:lo.y,(corner&4)?hi.z:lo.z);
          assert(B[i].size+.001f>=2.f*length(point-B[i].cg));
        }
      if (!(B[i].cg.x >= lo.x - 0.01f && B[i].cg.x <= hi.x + 0.01f && B[i].cg.y >= lo.y - 0.01f && B[i].cg.y <= hi.y + 0.01f && B[i].cg.z >= lo.z - 0.01f && B[i].cg.z <= hi.z + 0.01f)) { printf("  the %s's centre of mass is outside its box\n", breakKindName(P[i].kind)); failures++; }
      if (hi.x - lo.x > s.span + 2.f || hi.z - lo.z > s.fusLen + 4.f) { printf("  the %s's box (%.1f x %.1f x %.1f) is bigger than the airframe\n", breakKindName(P[i].kind), hi.x - lo.x, hi.y - lo.y, hi.z - lo.z); failures++; }
    }
    assert(fabsf(sum - mass) < 0.05f * mass + 3.f * n);
    // and they fly down: from 2,500 m at the type's speed (the research jets' 300 m/s), flung apart a little and spinning,
    // in still air - where no piece may ever gain energy (the air only takes it) - and every one comes down
    const float V0 = s.special || s.designMach > 1.f ? 300.f : s.cruise;
    float tDown[kMaxBreakPieces] = {}, vEnd[kMaxBreakPieces] = {}, spin[kMaxBreakPieces] = {};
    Rng r(77 + t);
    auto t0 = std::chrono::steady_clock::now(); int steps = 0;
    for (int i = 0; i < n; i++) {
      const DebrisBody& b = B[i];
      quat q; vec3 p = b.cg + vec3(0, 2500, 0);
      vec3 out = P[i].C; out.y = 0; out = length(out) > 0.1f ? normalize(out) : vec3(0, 1, 0);
      vec3 v = vec3(0, 0, -V0) + out * r.range(8.f, 20.f), w = normalize(vec3(r.range(-1, 1), r.range(-1, 1), r.range(-1, 1))) * r.range(2.f, 6.f);
      auto energy = [&]() { vec3 wb = q.conj().rotate(w); return 0.5f * b.mass * dot(v, v) + b.mass * G0 * p.y + 0.5f * (b.I.x * wb.x * wb.x + b.I.y * wb.y * wb.y + b.I.z * wb.z * wb.z); };
      float t = 0, spinSum = 0, E = energy(), gain = 0, t500 = -1; int spinN = 0;
      while (p.y > 0.f && t < 900.f) {
        debrisStep(b, p, v, q, w, vec3(0), isaDensity(p.y), 1.f / 60.f); t += 1.f / 60.f; steps++;
        if (!finite3(p) || !finite3(v) || !finite3(w)) { printf("  %s: not finite at %.1f s\n", breakKindName(P[i].kind), t); failures++; break; }
        const float E1 = energy(); gain = std::max(gain, (E1 - E) / std::max(E, 1.f)); E = std::min(E, E1);
        if (p.y < 600.f) { spinSum += length(w); spinN++; }
        if (t500 < 0.f && p.y < 500.f) t500 = t;
      }
      tDown[i] = t; vEnd[i] = t500 >= 0.f && t > t500 ? 500.f / (t - t500) : -v.y; spin[i] = spinN ? spinSum / spinN : 0.f;   // (vEnd: its average through the last 500 m)
      if (t >= 900.f) { printf("  the %s never came down (%.0f m left)\n", breakKindName(P[i].kind), p.y); failures++; }
      if (gain > 0.002f) { printf("  the %s gained %.1f%% of its energy\n", breakKindName(P[i].kind), gain * 100.f); failures++; }
      // a lightly loaded wing or tail surface never darts down (edge-on, diving, it can come down at a few tens of m/s);
      // a compact heavy piece (an engine in it) falls fast
      float area = 0; for (const auto& pl : b.plates) area += pl.chord * pl.span;
      const float loading = area > 0.f ? b.mass / area : 1e9f;
      if ((P[i].kind == BK_WING || P[i].kind == BK_TAIL || P[i].kind == BK_FIN) && loading < 12.f && vEnd[i] > 45.f) { printf("  the %s (%.0f kg/m^2) came down at %.0f m/s\n", breakKindName(P[i].kind), loading, vEnd[i]); failures++; }
      if (P[i].kind == BK_NACELLE && vEnd[i] < 40.f) { printf("  the nacelle (%.0f kg) came down at only %.0f m/s\n", b.mass, vEnd[i]); failures++; }
    }
    const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count() / std::max(steps, 1);
    printf("  falls:");
    for (int i = 0; i < n; i++) printf(" %s%s %.0fs %.0fm/s %.1frad/s;", P[i].side < 0 ? "L " : P[i].side > 0 ? "R " : "", breakKindName(P[i].kind), tDown[i], vEnd[i], spin[i]);
    printf("  (%.1f us a step)\n", us);
  }
  // a torn skin panel: flutters and drifts down at a few metres a second
  {
    DebrisBody b = debrisPanel(0.5f, 3.f);
    quat q = quat::axisAngle(vec3(1, 0, 0), 0.3f); vec3 p(0, 1000, 0), v(60, -5, 0), w(0.5f, 0, 1);
    float t = 0, vMin = 1e9f, vMax = -1e9f, spinMax = 0;
    while (p.y > 0.f && t < 1200.f) {
      debrisStep(b, p, v, q, w, vec3(0), isaDensity(p.y), 1.f / 60.f); t += 1.f / 60.f;
      assert(finite3(p) && finite3(v) && finite3(w));
      if (t > 20.f) { vMin = std::min(vMin, -v.y); vMax = std::max(vMax, -v.y); spinMax = std::max(spinMax, length(w)); }
    }
    printf("skin panel 0.5 m, 3 kg/m^2: down from 1000 m in %.0f s, sinking %.1f..%.1f m/s, spinning up to %.1f rad/s\n", t, vMin, vMax, spinMax);
    assert(t < 1200.f && vMin > 0.5f && vMax < 25.f);
  }
  if (failures) { printf("breakup_test: %d failures\n", failures); return 1; }
  printf("breakup_test: every type comes apart by component, each surface with its piece, and every piece flies down\n");
  return 0;
}
