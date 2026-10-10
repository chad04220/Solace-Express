// Solace Express - how an airframe comes apart, and how its pieces fly (breakup.h)
#include "breakup.h"
#include "aircraft.h"
#include "models.h"
#include "aero.h"
#include "gear_breakup_geometry.h"
#include <algorithm>
#include <cmath>

namespace {
struct Bounds {
  vec3 lo = vec3(1e9f), hi = vec3(-1e9f);
  bool any() const { return lo.x <= hi.x; }
  void add(vec3 p) { lo = vec3(std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)); hi = vec3(std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)); }
  void pad(vec3 d) { lo -= d; hi += d; }
};
BreakPiece fromBounds(const Bounds& b, int kind, int side) { return {(b.lo + b.hi) * 0.5f, (b.hi - b.lo) * 0.5f, kind, side}; }
BreakPiece fromLoHi(vec3 lo, vec3 hi, int kind, int side) { return {(lo + hi) * 0.5f, (hi - lo) * 0.5f, kind, side}; }
bool inBox(const BreakPiece& p, vec3 b) { vec3 d = b - p.C; return fabsf(d.x) <= p.H.x && fabsf(d.y) <= p.H.y && fabsf(d.z) <= p.H.z; }
float hwAt(const ModelDef& m, float z) { float hw, hh, cy; modelSection(m, z, hw, hh, cy); return hw; }
float topAt(const ModelDef& m, float z) { float hw, hh, cy; modelSection(m, z, hw, hh, cy); return cy + hh; }
float botAt(const ModelDef& m, float z) { float hw, hh, cy; modelSection(m, z, hw, hh, cy); return cy - hh; }
// a strip's extent: its chord (and a deflected control surface's travel), its share of the span, its thickness
void addStrip(Bounds& b, const AeroStrip& st, float tc) {
  const float half = 0.5f * st.area / std::max(st.chord, 0.05f), th = 0.5f * tc * st.chord + 0.04f + 0.18f * st.chord * (st.ctl != AC_NONE ? 1.f : 0.f);
  const vec3 le = st.r - st.c * (0.25f * st.chord), te = st.r + st.c * (0.75f * st.chord + (st.ctl == AC_FLAP ? 0.15f * st.chord : 0.f));
  for (int k = 0; k < 8; k++) b.add(((k & 1) ? te : le) + st.sp * ((k & 2) ? half : -half) + st.n * ((k & 4) ? th : -th));
}
}

const char* breakKindName(int kind) {
  static const char* n[BK_COUNT] = {"propeller", "nacelle", "gear leg", "strut", "canard", "tailplane", "fin", "wing", "nose", "tail cone", "centre section"};
  return kind >= 0 && kind < BK_COUNT ? n[kind] : "?";
}

int breakOwner(const BreakPiece* p, int n, vec3 b) {
  for (int i = 0; i < n - 1; i++) if (!p[i].rigidOnly && inBox(p[i], b)) return i;
  return n - 1;   // (the centre section: its own box, and whatever no other box holds)
}

int breakPieces(const AircraftSpec& s, float gearDown, float gearHeight, BreakPiece out[kMaxBreakPieces]) {
  const ModelDef& m = kModels[&s - kAircraft];
  int n = 0;
  auto push = [&](const BreakPiece& p) { if (n < kMaxBreakPieces - 3 && p.H.x > 0.f && p.H.y > 0.f && p.H.z > 0.f) out[n++] = p; };
  // the wing's chordwise extent at the root (its most forward point: a forward-swept wing's tips reach ahead of its root)
  const float zA = m.wing[5] + std::min(0.f, m.wing[3]) - 0.25f, zB = m.wing[5] + std::max(m.wing[1], m.wing[3] + m.wing[2]) + 0.35f;
  const float xr = modelHalfWidth(m, m.wing[5] + m.wing[1] * 0.5f) * 1.08f + 0.05f;
  const float z0 = m.st[0][0] - 2.f, z1 = m.st[7][0] + 2.f;
  float xs = std::max(m.wing[0], m.ht[0]) + 1.f, y0 = -(gearHeight + 1.f), y1 = 0.f;
  for (int i = 0; i < 8; i++) y1 = std::max(y1, m.st[i][3] + m.st[i][2]);
  y1 = std::max(y1, m.vt[4] + m.vt[0]) + 1.5f;
  // Wheels are at the full half-track (not half of that again). All gear legs
  // use separate rigid meshes: give them explicit debris owners once exposed,
  // without cutting a gear-shaped box out of the surrounding fuselage/wing skin.
  // At gear <= .2 the wheels are fully stowed while only the doors move; preserve
  // their existing attachment to the containing airframe piece in that state.
  auto gearPieces = [&]() {
    const bool fixed = !s.special && m.gear <= 2;
    if (!fixed && gearDown <= .2f) return;
    const GearStations gs = gearStations(s);
    const bool atlas = &s == &kAircraft[kAtlas];
    const float wr = s.special ? .38f : m.wheelR;
    const auto main = gearBreakup::mainPose(s,m,gs,gearHeight,gearDown);
    const vec3 wheel(gs.track,wr-gearHeight,gs.mainZ);
    auto place = [](vec3 p, vec3 hinge, const quat& rotation) { return hinge+rotation.rotate(p-hinge); };
    auto grow = [](Bounds& b,vec3 p,vec3 h) { b.add(p-h);b.add(p+h); };
    auto cylinderBounds = [&](Bounds& b,vec3 p,float radius,float half,vec3 hinge,const quat& rotation) {
      const vec3 axis=rotation.rotate(vec3(1,0,0));
      const vec3 h(fabsf(axis.x)*half+radius*sqrtf(std::max(0.f,1-axis.x*axis.x)),
                   fabsf(axis.y)*half+radius*sqrtf(std::max(0.f,1-axis.y*axis.y)),
                   fabsf(axis.z)*half+radius*sqrtf(std::max(0.f,1-axis.z*axis.z)));
      grow(b,place(p,hinge,rotation),h+vec3(.015f));
    };
    Bounds mainBounds;
    // The leg's endpoints/links and each complete wheel, transformed before making
    // their contact AABB. Rotating a deployed AABB leaves large empty corners and
    // makes a partly folded leg land above the ground or rotate around empty air.
    const float shaft = fixed ? .08f : atlas ? .24f : .20f;
    grow(mainBounds,main.hinge,vec3(shaft));
    grow(mainBounds,place(wheel,main.hinge,main.rotation),vec3(shaft));
    // Atlas uses a vertical oleo and a lower trailing link. The knee is not
    // collinear with hinge/contact, so include it before forming posed bounds.
    const vec3 mainKnee(gs.track,-2.40f,2.72f);
    if(atlas) grow(mainBounds,place(mainKnee,main.hinge,main.rotation),vec3(shaft));
    if(atlas) for(float x:{-.34f,.34f}) for(float z:{-.62f,.62f})
      cylinderBounds(mainBounds,wheel+vec3(x,0,z),wr,.189f,main.hinge,main.rotation);
    else if(m.gear==3 && !s.special) for(float x:{-.22f,.22f})
      cylinderBounds(mainBounds,wheel+vec3(x,0,0),wr,.154f,main.hinge,main.rotation);
    else cylinderBounds(mainBounds,wheel,wr,fixed?(m.gear==0?.065f:m.gear==1?.134f:.184f):.144f,main.hinge,main.rotation);
    if(fixed && m.gear==0) grow(mainBounds,wheel+vec3(0,.04f,.06f),vec3(.14f,wr*1.05f,wr*1.9f));
    if(fixed && m.gear==2) { grow(mainBounds,main.hinge+vec3(0,0,-.35f),vec3(.06f));grow(mainBounds,main.hinge+vec3(0,0,.30f),vec3(.06f)); }
    if(m.gear==3 && !s.special) {
      // The nacelle's narrow skin door and brackets ride on the leg. Their field
      // is authored in the stowed frame, then inverse-posed for its rigid bake.
      const auto nac=gearBreakup::nacFold(m,gs,gearHeight);
      const quat stowed=quat::axisAngle(vec3(1,0,0),nac.angle);
      for(int iz=0;iz<=12;++iz) for(float x:{-.125f,0.f,.125f}) {
        const float z=nac.z1+(nac.zs-nac.z1)*iz/12.f;
        const vec2 section=gearBreakup::nacSection(m,z);
        const vec3 skin(gs.track+x,section.x-sqrtf(std::max(0.f,section.y*section.y-x*x))+.012f,z);
        const vec3 rest=place(skin,nac.hinge,stowed.conj());
        grow(mainBounds,place(rest,main.hinge,main.rotation),vec3(.04f));
      }
    }
    const vec3 mainMass=place(lerp(main.hinge,wheel,.68f)+(atlas?(mainKnee-main.hinge)*.15f:vec3(0)),main.hinge,main.rotation);
    for(int sd:{-1,1}) {
      Bounds b=mainBounds;
      if(sd<0) { b.lo.x=-mainBounds.hi.x;b.hi.x=-mainBounds.lo.x; }
      BreakPiece p=fromBounds(b,BK_GEAR,sd);p.rigidOnly=true;p.massCenter=mainMass;p.massCenter.x*=sd;push(p);
    }
    const float z=s.taildragger?gs.tailZ:gs.noseZ;
    const float nwr=s.taildragger?.10f:s.special?.33f:wr*(atlas?.74f:m.gear==3?.75f:.85f);
    const vec3 nw(0,nwr-gearHeight+(s.taildragger?.11f*s.fusLen:0.f),z);
    float hw,hh,cy;modelSection(m,z-(s.taildragger?.3f:0.f),hw,hh,cy);
    vec3 pivot(0,cy-hh*(s.taildragger?.6f:.7f),z-(s.taildragger?.3f:.05f));
    quat nr;
    if(!fixed) {
      float py=s.special?gearBreakup::jetBelly(m,z,0.f)+.40f:botAt(m,z)+nwr+.07f;
      for(int i=0;i<2;++i) py=s.special?std::max(gearBreakup::jetBelly(m,z,0.f),gearBreakup::jetBelly(m,z+py-nw.y,0.f))+.40f:
                                              std::max(botAt(m,z),botAt(m,z+py-nw.y))+nwr+.07f;
      pivot=vec3(0,py,z);nr=quat::axisAngle(vec3(1,0,0),-.5f*PI*clampf((1-gearDown)*1.25f,0,1));
    }
    Bounds nose;grow(nose,pivot,vec3(.20f));grow(nose,place(nw,pivot,nr),vec3(.12f));
    const float offset=s.special?.10f:atlas?.25f:m.gear==3?.15f:0.f;
    const float half=s.taildragger?.079f:s.special?.114f:atlas?.159f:m.gear==3?.114f:.099f;
    // The breakup API has no steering angle. Bound the entire permitted steering
    // sweep, with extra travel margin, so a turned tyre/pant never lies outside it.
    const vec3 steeringPivot=fixed?vec3(0,0,z):pivot;
    for(float steer:{-.6f,-.45f,0.f,.45f,.6f}) {
      const quat turn=nr*quat::axisAngle(vec3(0,1,0),steer);
      for(float side:{-1.f,1.f}) cylinderBounds(nose,nw+vec3(side*offset,0,0),nwr,half,steeringPivot,turn);
      if(fixed && m.gear==0) {
        const vec3 radii(.13f,nwr*1.12f,nwr*2.2f),x=turn.rotate(vec3(radii.x,0,0)),y=turn.rotate(vec3(0,radii.y,0)),zz=turn.rotate(vec3(0,0,radii.z));
        const vec3 h(sqrtf(x.x*x.x+y.x*y.x+zz.x*zz.x),sqrtf(x.y*x.y+y.y*y.y+zz.y*zz.y),sqrtf(x.z*x.z+y.z*y.z+zz.z*zz.z));
        grow(nose,place(nw+vec3(0,.05f,.06f),steeringPivot,turn),h+vec3(.015f));
      }
    }
    BreakPiece p=fromBounds(nose,BK_GEAR,0);p.rigidOnly=true;p.massCenter=place(lerp(pivot,nw,.68f),pivot,nr);push(p);

  };
  if (s.special) {
    // the XR-30 and XR-40 draw their own airframes (mapJet, the Wraith's): their pieces fitted to those - nose, centre,
    // tail and both outer wings, no overlaps
    gearPieces();
    const float y0j = -1.7f, y1j = 2.9f;
    out[n++] = fromLoHi(vec3(-5.9f, -1.1f, -3.f), vec3(-2.2f, 0.8f, 6.1f), BK_WING, -1);
    out[n++] = fromLoHi(vec3(2.2f, -1.1f, -3.f), vec3(5.9f, 0.8f, 6.1f), BK_WING, 1);
    out[n++] = fromLoHi(vec3(-2.2f, y0j, -9.6f), vec3(2.2f, y1j, -3.f), BK_NOSE, 0);
    out[n++] = fromLoHi(vec3(-2.2f, y0j, 3.6f), vec3(2.2f, y1j, 9.6f), BK_AFT, 0);
    out[n++] = fromLoHi(vec3(-2.2f, y0j, -3.f), vec3(2.2f, y1j, 3.6f), BK_CENTRE, 0);
    return n;
  }
  const AeroGeom& g = aeroGeom(s);
  const float tcw = aeroModel(s).tc;
  // propellers (and their spinners): the blades' disc
  float pr[2][4]; const int np = modelProps(m, pr);
  for (int i = 0; i < np; i++) push({vec3(pr[i][0], pr[i][1], pr[i][2] + 0.05f), vec3(pr[i][3] + 0.1f, pr[i][3] + 0.1f, 0.26f), BK_PROP, pr[i][0] < -0.1f ? -1 : pr[i][0] > 0.1f ? 1 : 0});
  // engine nacelles that stand clear of the fuselage: on the wings, or podded on the tail cone
  const bool podded = m.engine >= 2 && m.nacR > 0.05f && m.nacX - m.nacR * 0.5f > hwAt(m, m.nacZ0 + 0.5f * m.nacLen);
  if (podded) for (int sd = -1; sd <= 1; sd += 2)
    push({vec3(sd * m.nacX, m.nacY, m.nacZ0 + 0.5f * m.nacLen), vec3(m.nacR + 0.1f, m.nacR + 0.14f, 0.5f * m.nacLen + (&s == &kAircraft[kAtlas] ? .50f : .12f)), BK_NACELLE, sd});
  gearPieces();
  // wing struts: from the lower fuselage out to the wing, below its skin
  if (m.strut) {
    const float zs = m.wing[5] + 0.3f * m.wing[1], yTop = m.wing[4] - 0.5f * tcw * m.wing[1] - 0.04f, yLow = botAt(m, zs) + 0.15f;
    for (int sd = -1; sd <= 1; sd += 2) {
      const float xi = hwAt(m, zs) + 0.03f, xo = m.strutX + 0.25f;
      if (yTop > yLow) push(fromLoHi(vec3(sd < 0 ? -xo : xi, yLow, zs - 0.5f), vec3(sd < 0 ? -xi : xo, yTop, zs + 0.5f), BK_STRUT, sd));
    }
  }
  // the lifting surfaces, from the strips the strip model flies
  float finHalf = 0.12f;
  for (int i = 0; i < g.nSt; i++) if (g.st[i].surf == AS_FIN) finHalf = std::max(finHalf, 0.06f * g.st[i].chord + 0.05f);
  auto tailHalves = [&](int surf, int kind) {
    for (int sd = -1; sd <= 1; sd += 2) {
      Bounds b; float zMid = 0, yMin = 1e9f; int k = 0;
      for (int i = 0; i < g.nSt; i++) {
        const AeroStrip& st = g.st[i];
        if (st.surf != surf || st.r.x * sd <= 0.f) continue;
        addStrip(b, st, 0.1f); zMid += st.r.z; yMin = std::min(yMin, st.r.y); k++;
      }
      if (!k) continue;
      zMid /= k;
      // a T-tail's half meets the fin, a low one the fuselage
      const float inner = yMin > topAt(m, zMid) + 0.05f ? finHalf + 0.02f : hwAt(m, zMid) + 0.02f;
      if (sd > 0) b.lo.x = inner; else b.hi.x = -inner;
      b.pad(vec3(0.f, 0.06f, 0.1f));
      if (sd > 0 ? b.hi.x > b.lo.x : b.lo.x < b.hi.x) push(fromBounds(b, kind, sd));
    }
  };
  tailHalves(AS_CANARD, BK_CANARD);
  tailHalves(AS_TAIL, BK_TAIL);
  {   // the fin (each of a pair), from the top of the fuselage under it, with the rudder's swing
    for (int sd = -1; sd <= 1; sd++) {
      Bounds b; float topF = -1e9f; int k = 0;
      for (int i = 0; i < g.nSt; i++) {
        const AeroStrip& st = g.st[i];
        if (st.surf != AS_FIN || (st.side < -0.5f ? -1 : st.side > 0.5f ? 1 : 0) != sd) continue;
        addStrip(b, st, 0.1f); k++;
      }
      if (!k) continue;
      for (float z = b.lo.z; z <= b.hi.z; z += 0.1f) topF = std::max(topF, topAt(m, z));
      b.pad(vec3(0.2f, 0.f, 0.12f)); b.hi.y += 0.1f;
      b.lo.y = std::max(b.lo.y, topF + 0.02f);   // (from the fuselage's top: its tail cone stays with the tail cone)
      if (b.hi.y > b.lo.y) push(fromBounds(b, BK_FIN, sd));
    }
  }
  {   // the wings, from the fuselage's side out: their strips outside the fuselage, the winglets, the flaps' travel
    for (int sd = -1; sd <= 1; sd += 2) {
      Bounds b; int k = 0;
      for (int i = 0; i < g.nSt; i++) {
        const AeroStrip& st = g.st[i];
        if (st.surf != AS_WING || st.r.x * sd <= hwAt(m, st.r.z)) continue;
        addStrip(b, st, tcw); k++;
      }
      if (!k) continue;
      b.lo.y = std::min(b.lo.y, m.wing[4] - 0.45f); b.hi.y = std::max(b.hi.y, m.wing[4] + 0.5f + m.winglet * 1.2f);
      b.lo.z = std::min(b.lo.z, zA); b.hi.z = std::max(b.hi.z, zB);
      if (sd > 0) { b.lo.x = xr; b.hi.x += 0.3f; } else { b.hi.x = -xr; b.lo.x -= 0.3f; }
      push(fromBounds(b, BK_WING, sd));
    }
  }
  // the fuselage in three: the nose (cockpit, cabin ahead of the wing, the engine in it), the tail cone, the centre section
  out[n++] = fromLoHi(vec3(-xs, y0, z0), vec3(xs, y1, zA), BK_NOSE, 0);
  out[n++] = fromLoHi(vec3(-xs, y0, zB), vec3(xs, y1, z1), BK_AFT, 0);
  out[n++] = fromLoHi(vec3(-xr, y0, zA), vec3(xr, y1, zB), BK_CENTRE, 0);
  return n;
}

// ---------------------------------------------------------------- where they tore
namespace {
float& at(vec3& v, int i) { return (&v.x)[i]; }
// points over the airframe's surfaces: each strip's leading edge, mid chord and trailing edge at its span's ends and
// middle, a ring round each fuselage slice at its ends and middle, and each engine
void airframePoints(const AircraftSpec& s, std::vector<vec3>& pts) {
  const AeroGeom& g = aeroGeom(s);
  for (int i = 0; i < g.nSt; i++) {
    const AeroStrip& st = g.st[i];
    const float half = 0.5f * st.area / std::max(st.chord, 0.05f);
    for (int a = 0; a < 3; a++) for (int b = 0; b < 3; b++) pts.push_back(st.r + st.c * (st.chord * (-0.25f + 0.5f * a)) + st.sp * (half * (b - 1)));
  }
  for (int k = 0; k < g.nSeg; k++) {
    const AeroSegment& sg = g.seg[k];
    for (int e = -1; e <= 1; e++) for (int a = 0; a < 16; a++) {
      const float t = a * (2.f * PI / 16.f);
      pts.push_back(vec3(0.5f * sg.w * cosf(t), sg.y + 0.5f * sg.h * sinf(t), sg.z + e * 0.5f * sg.len));
    }
  }
  for (int k = 0; k < g.nEng; k++) pts.push_back(g.eng[k].pos);
}
BreakCut facePatch(const BreakPiece& p, int f) {   // the whole of a box's face f (-x, +x, -y, +y, -z, +z)
  const int ax = f >> 1; BreakCut c{p.C, p.H};
  at(c.C, ax) += (f & 1 ? 1.f : -1.f) * p.H[ax]; at(c.H, ax) = 0.01f;
  return c;
}
}

int breakCuts(const AircraftSpec& s, const BreakPiece* p, int n, int k, BreakCut out[kMaxBreakCuts]) {
  int m = 0;
  auto add = [&](const BreakCut& c) { if (m < kMaxBreakCuts) out[m++] = c; };
  if (p[k].rigidOnly) { add(facePatch(p[k], 3)); return m; }
  if (s.special) {   // (the research jets' boxes only touch: where two meet face to face)
    for (int j = 0; j < n; j++) {
      if (j == k) continue;
      for (int f = 0; f < 6; f++) {
        const int ax = f >> 1, a1 = (ax + 1) % 3, a2 = (ax + 2) % 3;
        const float face = p[k].C[ax] + (f & 1 ? 1.f : -1.f) * p[k].H[ax], other = p[j].C[ax] - (f & 1 ? 1.f : -1.f) * p[j].H[ax];
        if (fabsf(face - other) > 0.05f) continue;
        const float lo1 = std::max(p[k].C[a1] - p[k].H[a1], p[j].C[a1] - p[j].H[a1]), hi1 = std::min(p[k].C[a1] + p[k].H[a1], p[j].C[a1] + p[j].H[a1]);
        const float lo2 = std::max(p[k].C[a2] - p[k].H[a2], p[j].C[a2] - p[j].H[a2]), hi2 = std::min(p[k].C[a2] + p[k].H[a2], p[j].C[a2] + p[j].H[a2]);
        if (hi1 <= lo1 || hi2 <= lo2) continue;
        BreakCut c; at(c.C, ax) = face; at(c.H, ax) = 0.01f;
        at(c.C, a1) = 0.5f * (lo1 + hi1); at(c.H, a1) = 0.5f * (hi1 - lo1); at(c.C, a2) = 0.5f * (lo2 + hi2); at(c.H, a2) = 0.5f * (hi2 - lo2);
        add(c);
      }
    }
    return m;
  }
  std::vector<vec3> pts; airframePoints(s, pts);
  std::vector<int> own(pts.size());
  for (size_t i = 0; i < pts.size(); i++) own[i] = breakOwner(p, n, pts[i]);
  // a face of piece j's box across which the airframe goes on into a later piece: the two were joined there, over the
  // stretch of the face those points cross (an earlier piece's points beyond it are its own box's business)
  for (int j = 0; j < n - 1; j++) {
    if (p[j].rigidOnly) continue;
    for (int f = 0; f < 6; f++) {
      const int ax = f >> 1, a1 = (ax + 1) % 3, a2 = (ax + 2) % 3;
      const float sg = f & 1 ? 1.f : -1.f, face = p[j].C[ax] + sg * p[j].H[ax];
      Bounds b; bool withK = false;
      for (size_t i = 0; i < pts.size(); i++) {
        if (own[i] <= j) continue;
        const vec3 q = pts[i]; const float d = (q[ax] - face) * sg;
        if (d <= 0.f || d > 0.6f || fabsf(q[a1] - p[j].C[a1]) > p[j].H[a1] || fabsf(q[a2] - p[j].C[a2]) > p[j].H[a2]) continue;
        b.add(q); withK = withK || own[i] == k;
      }
      if (!b.any() || (j != k && !withK)) continue;
      b.pad(vec3(0.15f));
      at(b.lo, ax) = face - 0.01f; at(b.hi, ax) = face + 0.01f;
      add({(b.lo + b.hi) * 0.5f, (b.hi - b.lo) * 0.5f});
    }
  }
  if (m == 0 && k < n - 1) {   // (no surface of the strip model reaches it: a gear leg tore off at its top, a strut at both ends, a propeller at its hub)
    const BreakPiece& q = p[k];
    if (q.kind == BK_GEAR) add(facePatch(q, 3));
    else if (q.kind == BK_STRUT) { add(facePatch(q, 0)); add(facePatch(q, 1)); }
    else if (q.kind == BK_PROP) add(facePatch(q, p[n - 1].C.z > q.C.z ? 5 : 4));
  }
  return m;
}

// ---------------------------------------------------------------- the pieces' bodies
namespace {
struct Elem { int kind; vec3 r; float mass; DebrisPlate pl; DebrisRod rod; float self; };   // kind 0 point, 1 plate, 2 rod
float gripOf(const DebrisBody& b) {   // (the air's hold at sea level per m/s of speed, 1/s per m/s; and per rad/s of spin)
  float a = 0;
  for (const auto& p : b.plates) a += 1.8f * p.chord * p.span;
  for (const auto& r : b.rods) a += 1.1f * r.len * std::max(r.w, r.h);
  a += 0.9f * b.blunt + 1.05f * 4.f * std::max(b.box.x * b.box.y, std::max(b.box.y * b.box.z, b.box.x * b.box.z));
  return 0.5f * 1.225f * a / std::max(b.mass, 1e-3f);
}
float spinGripOf(const DebrisBody& b) {
  float k = 0;
  for (const auto& p : b.plates) { float r = length(p.r); k += 1.225f * 1.8f * p.span * powf(p.chord, 4) / 64.f + 0.5f * 1.225f * 1.8f * p.chord * p.span * r * r * r; }
  for (const auto& rd : b.rods) { float r = length(rd.r) + 0.5f * rd.len; k += 0.5f * 1.225f * 1.1f * rd.len * std::max(rd.w, rd.h) * r * r * r; }
  const float L = 2.f * std::max(b.box.x, std::max(b.box.y, b.box.z));
  k += 0.1f * 1.225f * powf(L, 5);
  return k / std::max(std::min(b.I.x, std::min(b.I.y, b.I.z)), 1e-4f);
}
}
static vec3 vmin(vec3 a, vec3 b) { return vec3(std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)); }
static vec3 vmax(vec3 a, vec3 b) { return vec3(std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)); }
static void finish(DebrisBody& b, const std::vector<Elem>& els) {
  float M = 0; vec3 c(0);
  for (const Elem& e : els) { M += e.mass; c += e.r * e.mass; }
  if (M <= 0.f) return;
  c = c / M; b.cg = c; b.mass = M;
  vec3 I(0), lo(1e9f), hi(-1e9f);
  auto grow = [&](vec3 a, vec3 h) { lo = vmin(lo, a - h); hi = vmax(hi, a + h); };
  for (const Elem& e : els) {
    if (e.kind == 0) grow(e.r, vec3(0.1f));
    if (e.kind == 1) {   // (the plate from its leading edge to its trailing edge, across its span)
      const DebrisPlate& p = e.pl;
      for (int k = 0; k < 4; k++) grow(p.r + p.c * ((k & 1) ? 0.75f * p.chord : -0.25f * p.chord) + p.s * ((k & 2) ? 0.5f * p.span : -0.5f * p.span), vec3(0.02f));
    }
    if (e.kind == 2) grow(e.rod.r, vec3(0.5f * e.rod.w, 0.5f * e.rod.h, 0.5f * e.rod.len));
    const vec3 d = e.r - c;
    I += vec3(d.y * d.y + d.z * d.z, d.x * d.x + d.z * d.z, d.x * d.x + d.y * d.y) * e.mass + vec3(e.self * e.mass);
    if (e.kind == 1) { DebrisPlate p = e.pl; p.r = p.r - c; b.plates.push_back(p); }
    if (e.kind == 2) { DebrisRod r = e.rod; r.r = r.r - c; b.rods.push_back(r); }
  }
  const float floorI = M * 0.02f;
  b.I = vec3(std::max(I.x, floorI), std::max(I.y, floorI), std::max(I.z, floorI));
  b.lo = lo; b.hi = hi;
  float ext = 0.3f;
  for (const auto& p : b.plates) ext = std::max(ext, length(p.r) + 0.5f * std::max(p.chord, p.span));
  for (const auto& r : b.rods) ext = std::max(ext, length(r.r) + 0.5f * std::max(r.len, std::max(r.w, r.h)));
  b.size = 2.f * ext;
}

void breakBodies(const AircraftSpec& s, const BreakPiece* p, int n, float mass, float payload, DebrisBody out[kMaxBreakPieces]) {
  const ModelDef& m = kModels[&s - kAircraft];
  const AeroGeom& g = aeroGeom(s);
  const float empty = std::max(mass - payload, 1.f);
  std::vector<Elem> els[kMaxBreakPieces];
  // the structure's weight, the way a light aircraft's divides: the wings (with their flaps, ailerons and the fuel
  // system) about a sixth of it, the tail surfaces a thirtieth, the engines and their mounts a sixth, the propellers,
  // the gear legs and struts a little each; the fuselage, its systems and the load aboard the rest
  float wingA = 0, tailA = 0;
  for (int i = 0; i < g.nSt; i++) (g.st[i].surf == AS_WING ? wingA : tailA) += g.st[i].area;
  const float mWing = 0.18f * empty, mTail = 0.035f * empty, mEng = 0.16f * empty, mProp = 0.015f * empty;
  float used = mWing + mTail + (g.nEng ? mEng : 0.f);
  for (int i = 0; i < g.nSt; i++) {
    const AeroStrip& st = g.st[i];
    Elem e; e.kind = 1; e.r = st.r + st.c * (0.2f * st.chord);   // (its weight at 45% of the chord - spar, skin, ribs and the control surfaces behind: aft of the quarter chord its lift acts at, so a torn wing pitches broadside)
    e.mass = st.surf == AS_WING ? mWing * st.area / std::max(wingA, 1e-3f) : mTail * st.area / std::max(tailA, 1e-3f);
    e.pl = {st.r, st.c, st.n, st.sp, st.chord, st.area / std::max(st.chord, 0.05f)};
    e.self = (st.chord * st.chord) / 12.f;
    els[breakOwner(p, n, st.r)].push_back(e);
  }
  // a torn surface's root - a wing's, a tail half's, a fin's - meets air flowing along its span with its end face
  for (int i = 0; i < n; i++) {
    if (p[i].kind != BK_WING && p[i].kind != BK_TAIL && p[i].kind != BK_CANARD && p[i].kind != BK_FIN) continue;
    int root = -1; float best = 1e9f;
    for (int k = 0; k < g.nSt; k++) {
      const AeroStrip& st = g.st[k];
      if (breakOwner(p, n, st.r) != i) continue;
      const float d = p[i].kind == BK_FIN ? st.r.y : fabsf(st.r.x);   // (nearest the fuselage)
      if (d < best) { best = d; root = k; }
    }
    if (root < 0) continue;
    const AeroStrip& st = g.st[root];
    const float th = std::max((p[i].kind == BK_WING ? aeroModel(s).tc : 0.1f) * st.chord, 0.04f);
    Elem e; e.kind = 2; e.r = st.r + st.c * (0.25f * st.chord); e.mass = 0.f; e.self = 0.f;
    // (as a slice along the body's z: its crossflow faces are the root's end face to a spanwise flow - sideways for
    // a wing or a tail half, up the fin)
    e.rod = {e.r, st.chord, th, th};
    els[i].push_back(e);
  }
  for (int k = 0; k < g.nEng; k++) {   // (a propeller's engine sits behind its hub; a jet's along its line)
    const AeroEngine& en = g.eng[k];
    Elem e; e.kind = 0; e.r = en.pos - en.fwd * (en.prop ? 0.7f : 0.f); e.mass = mEng / g.nEng; e.self = 0.1f;
    els[breakOwner(p, n, e.r)].push_back(e);
  }
  for (int i = 0; i < n; i++) {   // the small parts' own weight, and the propeller's blades as a plate across its axis
    float pm = p[i].kind == BK_PROP ? mProp : p[i].kind == BK_GEAR ? (p[i].side ? 0.018f : 0.01f) * empty : p[i].kind == BK_STRUT ? 0.007f * empty : 0.f;
    if (pm <= 0.f) continue;
    used += pm;
    Elem e; e.kind = 0; e.r = p[i].rigidOnly ? p[i].massCenter : p[i].C; e.mass = pm; e.self = (p[i].H.x * p[i].H.x + p[i].H.y * p[i].H.y + p[i].H.z * p[i].H.z) / 3.f;
    els[i].push_back(e);
    if (p[i].kind == BK_PROP) {   // two blades' worth of plate in the disc
      Elem b; b.kind = 1; b.r = p[i].C; b.mass = 0.f; b.self = 0.f;
      b.pl = {p[i].C, vec3(0, 1, 0), vec3(0, 0, -1), vec3(1, 0, 0), 0.14f * p[i].H.x, 2.f * p[i].H.x};
      els[i].push_back(b);
    }
  }
  // the fuselage, slice by slice: its weight by volume, its crossflow as bluff slices
  const float mFus = std::max(mass - used, 0.2f * mass);
  for (int k = 0; k < g.nSeg; k++) {
    const AeroSegment& sg = g.seg[k];
    Elem e; e.kind = 2; e.r = vec3(0, sg.y, sg.z);
    e.mass = mFus * sg.area * sg.len / std::max(g.fusVol, 1e-3f);
    e.rod = {e.r, sg.len, sg.w, sg.h}; e.self = (sg.len * sg.len) / 12.f;
    els[breakOwner(p, n, e.r)].push_back(e);
  }
  for (int i = 0; i < n; i++) {
    DebrisBody& b = out[i];
    b = DebrisBody();
    finish(b, els[i]);
    if (els[i].empty() || b.mass <= 0.f) {   // nothing of its own (a fairing, a pod): a little of the structure as a box
      b.mass = std::max(0.004f * empty, 1.f); b.cg = p[i].C;
      b.I = vec3(b.mass * 0.1f); b.box = p[i].H; b.size = 2.f * length(p[i].H);
      b.lo = p[i].C - p[i].H; b.hi = p[i].C + p[i].H;
    }
    // (a propeller, a gear leg, a strut or a pod is the whole of its box)
    if (p[i].kind == BK_PROP || p[i].kind == BK_GEAR || p[i].kind == BK_STRUT || p[i].kind == BK_NACELLE) { b.lo = vmin(b.lo, p[i].C - p[i].H); b.hi = vmax(b.hi, p[i].C + p[i].H); }
    if (p[i].rigidOnly) {
      // A complete gear assembly has only a point-mass element, so finish()
      // cannot infer its extent from plates/rods. Use its posed bounds about the
      // true mass centre rather than treating a multi-metre bogie as a .6 m chip.
      // This feeds the existing breakup impulse scaling and rim-speed spin cap.
      const vec3 radius(std::max(fabsf(b.lo.x-b.cg.x),fabsf(b.hi.x-b.cg.x)),
                        std::max(fabsf(b.lo.y-b.cg.y),fabsf(b.hi.y-b.cg.y)),
                        std::max(fabsf(b.lo.z-b.cg.z),fabsf(b.hi.z-b.cg.z)));
      b.size = std::max(b.size, 2.f*length(radius));
    }
    // a fuselage piece's torn ends meet the air broadside along its axis
    auto section = [&](float z) { float hw, hh, cy; modelSection(m, z, hw, hh, cy); return PI * hw * hh; };
    const float zA = p[n - 1].C.z - p[n - 1].H.z, zB = p[n - 1].C.z + p[n - 1].H.z;
    if (p[i].kind == BK_NOSE) b.blunt = section(zA);
    if (p[i].kind == BK_AFT) b.blunt = section(zB);
    if (p[i].kind == BK_CENTRE) b.blunt = 0.5f * (section(zA) + section(zB));
    if (b.plates.empty() && b.rods.empty() && b.box.x <= 0.f) b.box = p[i].H * 0.6f;   // (a lump: at least something for the air)
  }
}

DebrisBody debrisPanel(float size, float sigma) { return debrisPanel(size, 0.8f * size, sigma); }
DebrisBody debrisPanel(float chord, float span, float sigma) {
  DebrisBody b;
  const float c = std::max(chord, 0.05f), sp = std::max(span, 0.05f), A = c * sp;
  b.mass = std::max(sigma * A, 0.01f);
  b.plates.push_back({vec3(0, 0, -0.25f * c), vec3(0, 0, 1), vec3(0, 1, 0), vec3(1, 0, 0), c, sp});   // (its quarter chord: a quarter ahead of the middle, where the mass is)
  b.I = vec3(b.mass * c * c / 12.f, b.mass * (c * c + sp * sp) / 12.f, b.mass * sp * sp / 12.f);
  b.size = std::max(c, sp);
  b.lo = vec3(-0.5f * sp, -0.05f * c, -0.5f * c); b.hi = -b.lo;
  return b;
}

void debrisAir(const DebrisBody& b, const quat& q, vec3 v, vec3 w, vec3 wind, float rho, vec3& F, vec3& M) {
  F = vec3(0); M = vec3(0);
  for (const DebrisPlate& pl : b.plates) {
    const vec3 r = q.rotate(pl.r), c = q.rotate(pl.c), nn = q.rotate(pl.n), s = q.rotate(pl.s);
    const float A = pl.chord * pl.span;
    // the air over it, in its chord's plane, first at its quarter chord: from it, where its centre of pressure is - back
    // from the quarter chord of whichever edge meets the air, to the middle broadside - and then the force from the air
    // at that point itself (the force from one point's air put on another's: a spinning plate pumped itself up)
    vec3 u = v + cross(w, r) - wind;
    float fc = -dot(u, c), fn = -dot(u, nn);
    vec3 Fp(0), at = r;
    if (fc * fc + fn * fn > 1e-4f) {
      const float sa0 = fabsf(fn) / sqrtf(fc * fc + fn * fn), xcp = pl.chord * (0.25f + 0.25f * sa0);
      const vec3 le = r - c * (0.25f * pl.chord);
      at = fc >= 0.f ? le + c * xcp : le + c * (pl.chord - xcp);
      u = v + cross(w, at) - wind; fc = -dot(u, c); fn = -dot(u, nn);
    }
    const float V2 = fc * fc + fn * fn, us = dot(u, s);
    if (V2 > 1e-4f) {
      const float V = sqrtf(V2), al = atan2f(fn, fc), sa = sinf(al), ca = cosf(al), qS = 0.5f * rho * V2 * A;
      // lift across the flow, turning round with the angle; drag along it, edge-on to broadside
      const vec3 fh = (c * fc + nn * fn) * (1.f / V), lh = c * (-sa) + nn * ca;
      Fp = lh * (qS * 1.2f * sinf(2.f * al)) + fh * (qS * (0.1f * ca * ca + 1.8f * sa * sa));
      // the circulation of its own spin about the span (a falling card's: it is what keeps one autorotating), no more
      // than a spin of one chord per chord travelled makes
      const float om = dot(w, s), omE = clampf(om, -V / pl.chord, V / pl.chord);
      Fp += cross(s * omE, fh * (-V)) * (0.25f * rho * PI * pl.chord * pl.chord * pl.span);   // (half the 2-D card's: a finite span's)
      // and its spin about the span slowed by the air it pushes broadside
      M += s * (-rho * 1.8f * pl.span * pl.chord * pl.chord * pl.chord * pl.chord / 64.f * om * fabsf(om));
    }
    Fp += s * (-0.5f * rho * 0.04f * A * us * fabsf(us));   // (friction along the span)
    F += Fp; M += cross(at, Fp);
  }
  const vec3 ex = q.rotate(vec3(1, 0, 0)), ey = q.rotate(vec3(0, 1, 0)), ez = q.rotate(vec3(0, 0, 1));
  for (const DebrisRod& rd : b.rods) {   // the fuselage's slices: crossflow drag, a little friction along the axis
    const vec3 r = q.rotate(rd.r), u = v + cross(w, r) - wind;
    const float ux = dot(u, ex), uy = dot(u, ey), uz = dot(u, ez), up = sqrtf(ux * ux + uy * uy);
    const vec3 Fr = (ex * (ux * rd.h) + ey * (uy * rd.w)) * (-0.5f * rho * 1.1f * rd.len * up) + ez * (-0.5f * rho * 0.006f * (rd.w + rd.h) * 2.f * rd.len * uz * fabsf(uz));
    F += Fr; M += cross(r, Fr);
  }
  const vec3 u0 = v - wind;
  if (b.blunt > 0.f) { const float uz = dot(u0, ez); F += ez * (-0.5f * rho * 0.9f * b.blunt * uz * fabsf(uz)); }   // (the torn ends)
  if (b.box.x > 0.f) {   // a lump: drag on each face's area, its tumble damped
    const float ux = dot(u0, ex), uy = dot(u0, ey), uz = dot(u0, ez), ul = length(u0);
    F += (ex * (ux * b.box.y * b.box.z) + ey * (uy * b.box.x * b.box.z) + ez * (uz * b.box.x * b.box.y)) * (-0.5f * rho * 1.05f * 4.f * ul);
    const float L = 2.f * std::max(b.box.x, std::max(b.box.y, b.box.z));
    M += w * (-0.1f * rho * powf(L, 5) * length(w));
  }
  { const float wz = dot(w, ez); M += ez * (-0.02f * rho * wz * fabsf(wz)); }   // (a little against a fuselage's roll)
}

void debrisStep(const DebrisBody& b, vec3& p, vec3& v, quat& q, vec3& w, vec3 wind, float rho, float dt) {
  // the air's grip: how quickly it would bring the body to the air's speed and its spin to rest - the substeps keep
  // each well inside that
  const float gL = gripOf(b), gR = spinGripOf(b);
  const float k = (gL * length(v - wind) + gR * length(w)) * (rho / 1.225f);
  const int nSub = std::clamp((int)ceilf(k * dt / 0.2f), 1, 64);
  const float h = dt / nSub;
  for (int i = 0; i < nSub; i++) {
    vec3 F, M; debrisAir(b, q, v, w, wind, rho, F, M);
    v += (F * (1.f / b.mass) + vec3(0, -G0, 0)) * h;
    // the spin through its angular momentum (the moment changes it; the spin is what that momentum makes of the
    // body's inertia as it is turned now - a free tumble keeps its momentum, and an explicit step of the gyroscopic
    // coupling gained energy at a fast spin)
    const vec3 wb0 = q.conj().rotate(w), Lw = q.rotate(vec3(b.I.x * wb0.x, b.I.y * wb0.y, b.I.z * wb0.z)) + M * h;
    const vec3 Lb = q.conj().rotate(Lw);
    vec3 wb(Lb.x / b.I.x, Lb.y / b.I.y, Lb.z / b.I.z);
    const float wMax = clampf(60.f / std::max(b.size, 0.1f), 3.f, 60.f), wl0 = length(wb);   // (no piece outspins a rim speed of 30 m/s)
    if (wl0 > wMax) wb = wb * (wMax / wl0);
    w = q.rotate(wb);
    p += v * h;
    const float wl = length(w);
    if (wl > 1e-6f) { q = quat::axisAngle(w, wl * h) * q; q.normalize(); }
  }
}
