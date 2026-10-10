// Solace Express - how an airframe comes apart, and how its pieces fly (breakup.h)
#include "breakup.h"
#include "aircraft.h"
#include "models.h"
#include "aero.h"
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
  for (int i = 0; i < n - 1; i++) if (inBox(p[i], b)) return i;
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
  if (s.special) {
    // the XR-30 and XR-40 draw their own airframes (mapJet, the Wraith's): their pieces fitted to those - nose, centre,
    // tail and both outer wings, no overlaps
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
    push({vec3(sd * m.nacX, m.nacY, m.nacZ0 + 0.5f * m.nacLen), vec3(m.nacR + 0.1f, m.nacR + 0.14f, 0.5f * m.nacLen + 0.12f), BK_NACELLE, sd});
  // the landing gear: fixed, or retractable and down
  const bool fixedGear = m.gear <= 2, gearOut = fixedGear || gearDown > 0.5f;
  if (gearOut) {
    const GearStations gs = gearStations(s);
    const float yb = -gearHeight - 0.08f, wr = std::max(m.wheelR, 0.2f);
    for (int sd = -1; sd <= 1; sd += 2) {
      const float x = sd * (m.gear == 3 && podded ? m.nacX : 0.5f * gs.track);
      float top = botAt(m, gs.mainZ) - 0.03f;
      if (m.wing[4] < 0.f) top = std::min(top, m.wing[4] - 0.5f * tcw * m.wing[1] - 0.03f);   // (under a low wing: below its skin)
      if (m.gear == 3 && podded) top = std::min(top, m.nacY - m.nacR - 0.02f);
      if (top > yb + 0.2f) push(fromLoHi(vec3(x - wr * 0.7f - 0.3f, yb, gs.mainZ - wr - 0.45f), vec3(x + wr * 0.7f + 0.3f, top, gs.mainZ + wr + 0.45f), BK_GEAR, sd));
    }
    if (!s.taildragger) {
      const float top = botAt(m, gs.noseZ) - 0.03f;
      if (top > yb + 0.2f) push(fromLoHi(vec3(-wr * 0.5f - 0.2f, yb, gs.noseZ - wr - 0.35f), vec3(wr * 0.5f + 0.2f, top, gs.noseZ + wr + 0.35f), BK_GEAR, 0));
    }
  }
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
static void finish(DebrisBody& b, const std::vector<Elem>& els) {
  float M = 0; vec3 c(0);
  for (const Elem& e : els) { M += e.mass; c += e.r * e.mass; }
  if (M <= 0.f) return;
  c = c / M; b.cg = c; b.mass = M;
  vec3 I(0);
  for (const Elem& e : els) {
    const vec3 d = e.r - c;
    I += vec3(d.y * d.y + d.z * d.z, d.x * d.x + d.z * d.z, d.x * d.x + d.y * d.y) * e.mass + vec3(e.self * e.mass);
    if (e.kind == 1) { DebrisPlate p = e.pl; p.r = p.r - c; b.plates.push_back(p); }
    if (e.kind == 2) { DebrisRod r = e.rod; r.r = r.r - c; b.rods.push_back(r); }
  }
  const float floorI = M * 0.02f;
  b.I = vec3(std::max(I.x, floorI), std::max(I.y, floorI), std::max(I.z, floorI));
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
    Elem e; e.kind = 0; e.r = p[i].C; e.mass = pm; e.self = (p[i].H.x * p[i].H.x + p[i].H.y * p[i].H.y + p[i].H.z * p[i].H.z) / 3.f;
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

DebrisBody debrisPanel(float size, float sigma) {
  DebrisBody b;
  const float c = std::max(size, 0.05f), sp = 0.8f * c, A = c * sp;
  b.mass = std::max(sigma * A, 0.01f);
  b.plates.push_back({vec3(0, 0, -0.25f * c), vec3(0, 0, 1), vec3(0, 1, 0), vec3(1, 0, 0), c, sp});   // (its quarter chord: a quarter ahead of the middle, where the mass is)
  b.I = vec3(b.mass * c * c / 12.f, b.mass * (c * c + sp * sp) / 12.f, b.mass * sp * sp / 12.f);
  b.size = c;
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
