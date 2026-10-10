// Solace Express - what stands at each airport: hangars, terminals, towers, fuel, parked aircraft, vehicles, fences
// and navigation aids, laid out on the ground plan in airport_layout.h. Built once per airport; the scenery chunks
// pick up the items that fall inside them.
#include "airport_layout.h"
#include "entities.h"
#include <cstring>

namespace {
struct Placer {
  const Airport& a; const AptLayout& L; std::vector<AptItem>& out; uint32_t rs;
  float rnd() { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return (rs & 0xFFFFFF) / 16777216.f; }
  float rnd(float lo, float hi) { return lo + (hi - lo) * rnd(); }
  float gnd(float x, float z) { return g_world.groundHeight(x, z, 7); }
  // an item at runway-local (u, vv) where vv is measured towards the facility side; its front faces (fu, fvv)
  void put(int kind, float u, float vv, float fu, float fvv, float sx, float sy, float sz, bool footprint = true, float seed = -1) {
    vec3 p = aptWorld(a, u, L.side * vv, 0);
    float yaw = aptYaw(a, fu, L.side * fvv);
    const EntKindInfo& I = kEntInfo[kind];
    float y = gnd(p.x, p.z);
    if (footprint) {   // lowest ground under the footprint, so nothing floats on a slope
      float c = cosf(yaw), s = sinf(yaw);
      for (int k = 0; k < 4; k++) {
        float lx = (k & 1 ? 1.f : -1.f) * I.hx * sx, lz = (k & 2 ? 1.f : -1.f) * I.hz * sz;
        y = std::min(y, gnd(p.x + c * lx + s * lz, p.z - s * lx + c * lz));
      }
    }
    out.push_back({kind, {p.x, y - (footprint ? 0.05f : 0.f), p.z, yaw, sx, sy, sz, seed >= 0 ? seed : rnd()}});
  }
  void putS(int kind, float u, float vv, float fu, float fvv, float s, bool footprint = true) { put(kind, u, vv, fu, fvv, s, s, s, footprint); }
  // building facing the runway with its front on the line vv = front
  void facing(int kind, float u, float front, float sx, float sy, float sz) { put(kind, u, front + kEntInfo[kind].hz * sz, 0, -1, sx, sy, sz); }
  // Conservative runway-local footprint, used for the new landside details. The original
  // airport plan/terrain is read only: if a site is occupied or unsuitable, omit the detail.
  struct Bounds { float u, v, hu, hv; };
  Bounds bounds(const AptItem& item) const {
    const Ent& e = item.e;
    const EntKindInfo& I = kEntInfo[item.kind];
    vec2 centre = aptLocal(a, {e.x, 0, e.z});
    float angle = e.yaw + a.heading * DEG;
    float c = fabsf(sinf(angle)), s = fabsf(cosf(angle));
    return {centre.x, centre.y * L.side, c * I.hx * e.sx + s * I.hz * e.sz,
            s * I.hx * e.sx + c * I.hz * e.sz};
  }
  static bool intersects(const Bounds& b, float u0, float u1, float v0, float v1, float pad = 0.f) {
    return b.u + b.hu + pad >= u0 && b.u - b.hu - pad <= u1 &&
           b.v + b.hv + pad >= v0 && b.v - b.hv - pad <= v1;
  }
  // Static parked life only. No moving ground actor enters flight, taxi, or emergency access space.
  bool detail(int kind, float u, float vv, float fu, float fvv, float sx = 1.f, float sy = 1.f, float sz = 1.f) {
    size_t oldSize = out.size();
    put(kind, u, vv, fu, fvv, sx, sy, sz);
    const AptItem item = out.back();
    Bounds b = bounds(item);
    bool safe = !intersects(b, -a.length * .5f - 60.f, a.length * .5f + 60.f,
                           -a.width * .5f - 25.f, a.width * .5f + 25.f);
    // Details stay behind the apron, never in an active stand, an exit or a taxi lane.
    if (L.paved) {
      safe &= !intersects(b, -a.length * .5f, a.length * .5f, L.twV - L.twHW - 3.f, L.apV1 - 8.f);
      safe &= !intersects(b, L.termU - 8.f, L.termU + 8.f, L.lotV0 - 4.f, L.lotV1 + 30.f);
    } else {
      safe &= !intersects(b, L.termU + 72.f, L.termU + 88.f, L.bldV + 25.f, L.bldV + 60.f);
    }
    // No new obstacles in either approach funnel, and no development outside the already
    // graded airport envelope. Terrain/collision heights are never changed to fit a prop.
    float end = fabsf(b.u) + b.hu - a.length * .5f;
    if (end > -60.f && fabsf(b.v) - b.hv < 250.f + .18f * std::max(0.f, end)) safe = false;
    if (fabsf(b.u) + b.hu > a.length * .5f + 180.f || fabsf(b.v) + b.hv > (a.size == 2 ? 420.f : 270.f)) safe = false;
    float low = 1e9f, high = -1e9f;
    for (int q = 0; q < 5; q++) {
      float du = q == 4 ? 0.f : (q & 1 ? b.hu : -b.hu), dv = q == 4 ? 0.f : (q & 2 ? b.hv : -b.hv);
      vec3 pos = aptWorld(a, b.u + du, L.side * (b.v + dv), 0);
      float y = gnd(pos.x, pos.z); low = std::min(low, y); high = std::max(high, y);
    }
    safe &= low > .2f && high - low < 1.25f;
    for (size_t i = 0; safe && i < oldSize; i++) {
      Bounds other = bounds(out[i]);
      safe = !intersects(b, other.u - other.hu, other.u + other.hu,
                        other.v - other.hv, other.v + other.hv, .65f);
    }
    if (!safe) out.pop_back();
    return safe;
  }
  // a row of cars in a car park: stalls along u, noses pointing (0, fvv)
  void cars(float u0, float u1, float vv, float fvv, float fill) {
    for (float u = u0; u <= u1; u += 2.7f) if (rnd() < fill) {
      float cu = u + rnd(-0.15f, 0.15f), cv = vv + rnd(-0.2f, 0.2f), face = rnd(-0.03f, 0.03f);
      // A continuous access aisle joins all parking rows to the landside gate. Keep the
      // stall-width clearance too, so no bumper clips the 16 m route.
      if (L.paved && vv >= L.lotV0 && fabsf(cu - L.termU) < 9.1f) continue;
      detail(EK_CAR, cu, cv, face, fvv);
    }
  }
  // perimeter fence along a runway-local polyline (vv on the facility side)
  void fence(float u0, float v0, float u1, float v1) {
    float len = sqrtf((u1 - u0) * (u1 - u0) + (v1 - v0) * (v1 - v0));
    if (len < .01f) return;
    int n = std::max(1, (int)(len / 20.f + 0.5f));
    for (int i = 0; i < n; i++) {
      float t = (i + 0.5f) / n, u = lerpf(u0, u1, t), v = lerpf(v0, v1, t);
      // the panel runs along its own x: face across the line
      float du = (u1 - u0) / len, dv = (v1 - v0) / len;
      put(EK_FENCE, u, v, dv, -du, len / n / 20.f, 1, 1, false);
    }
  }
};

void navaids(Placer& P, const Airport& a, const AptLayout& L, bool both) {
  float hl = a.length * 0.5f, hw = a.width * 0.5f;
  for (int e = 1; e >= (both ? -1 : 1); e -= 2) {
    P.put(EK_LOCALIZER, e * (hl + 390.f), 0, -e, 0, 1, 1, 1);               // beyond the far end, facing down the runway
    P.put(EK_MAST, -e * (hl - 300.f), -(hw + 120.f), 0, 1, 1, 1, 1);       // glide slope beside the touchdown zone
  }
}

void paved(Placer& P, const Airport& a, const AptLayout& L) {
  bool big = a.size == 2;
  float hl = a.length * 0.5f, hw = a.width * 0.5f;
  float tU0 = L.termU - L.termHL, tU1 = L.termU + L.termHL;
  // ---- terminal, and the gates in front of it
  P.facing(EK_TERMINAL, L.termU, L.bldV, L.termHL / 60.f, big ? 1.f : 0.62f, big ? 1.f : 0.7f);
  if (big) {
    int n = std::max(2, (int)(L.termHL * 2.f / 54.f));
    float span = n * 54.f;
    for (int i = 0; i < n; i++) {
      float gu = L.termU - span * 0.5f + 27.f + i * 54.f, s = P.rnd() < 0.3f ? 0.86f : 1.f;
      if (P.rnd() < 0.18f) continue;   // an empty gate
      float noseV = L.bldV - 8.f;
      P.put(EK_AIRLINER, gu, noseV - 19.5f * s, 0, 1, s, s, s);
      // jet bridge from the facade to the forward door (left side, 5.5 m behind the nose)
      vec3 fw = aptWorld(a, 0, L.side, 0) - aptWorld(a, 0, 0, 0), lf(fw.z, 0, -fw.x);
      vec3 door = aptWorld(a, gu, L.side * (noseV - 5.5f * s), 0) + lf * (2.6f * s);
      vec2 dl = aptLocal(a, door);
      float du = dl.x, dv = dl.y * L.side;
      float tu = du + (du > gu ? 7.f : -7.f), tv = L.bldV + 1.5f;
      float lenB = sqrtf((du - tu) * (du - tu) + (dv - tv) * (dv - tv));
      P.put(EK_JETBRIDGE, (du + tu) * 0.5f, (dv + tv) * 0.5f, du - tu, dv - tv, 1, 1, lenB / 20.f, false);
      if (P.rnd() < 0.6f) P.put(EK_TRUCK, gu + 14.f * (P.rnd() < 0.5f ? -1.f : 1.f), noseV - 16.f, 0, -1, 1, 1, 1, false);
    }
  } else {   // regional: a turboprop / regional jet parked off the terminal, passengers walk out
    P.put(EK_AIRLINER, L.termU - L.termHL * 0.4f, L.bldV - 16.f - 19.5f * 0.68f, 0, 1, 0.68f, 0.68f, 0.68f);
    if (P.rnd() < 0.5f) P.put(EK_AIRLINER, L.termU + L.termHL * 0.6f, L.bldV - 16.f - 19.5f * 0.62f, 0, 1, 0.62f, 0.62f, 0.62f);
  }
  // ---- control tower behind the terminal end
  P.put(EK_CTRL_TOWER, tU1 + (big ? 60.f : 26.f), L.bldV + (big ? 40.f : 16.f), 0, -1, 1, big ? 1.35f : 0.68f, 1);
  // ---- building line west of the terminal: cargo (big), FBO, T-hangars, hangars; east: maintenance hangars
  float u = L.apU0 + 6.f;
  if (big) for (int i = 0; i < 2; i++) {   // cargo sheds, a freighter nosed in at each
    P.facing(EK_WAREHOUSE, u + 30.f, L.bldV, 2.5f, 1.4f, 2.f);
    if (P.rnd() < 0.85f) P.put(EK_AIRLINER, u + 30.f, L.bldV - 10.f - 19.5f, 0, 1, 1, 1, 1);
    u += 66.f;
  }
  if (u + 48.f < L.tieU1 + 30.f) { P.facing(EK_T_HANGAR, u + 24.f, L.bldV, 1, 1, 1); u += 52.f; }
  if (!big && u + 48.f < tU0 - 60.f) { P.facing(EK_T_HANGAR, u + 24.f, L.bldV, 1, 1, 1); u += 52.f; }
  if (u + 20.f < tU0 - 8.f) { P.facing(EK_FBO, u + 10.f, L.bldV + 4.f, 1, 1, 1); u += 26.f; }
  while (u + 30.f < tU0 - 10.f) { float s = P.rnd(0.6f, 0.85f); P.facing(EK_HANGAR, u + 20.f * s, L.bldV, s, s, s); u += 40.f * s + 6.f; }
  u = tU1 + (big ? 110.f : 52.f);
  while (u + 30.f < L.apU1) { float s = big ? P.rnd(1.1f, 1.5f) : P.rnd(0.7f, 1.f); P.facing(EK_HANGAR, u + 20.f * s, L.bldV, s, s, s); u += 40.f * s + 8.f; }
  // fire station off the end of the apron
  P.facing(EK_HANGAR, L.apU1 + 40.f, L.apV0 + 10.f, 0.55f, 0.5f, 0.5f);
  // ---- GA tie-downs: rows of parked singles (the AI stands are kept clear further along)
  for (int row = 0; row < 2; row++) {
    float vv = L.apV0 + 24.f + row * 22.f;
    for (float tu = L.tieU0 + 7.f; tu < L.tieU1 - 6.f; tu += 14.f)
      if (P.rnd() < 0.72f) P.put(EK_GA_PLANE, tu, vv, 0, row ? 1.f : -1.f, 1, 1, 1, false);
  }
  // ---- fuel: farm behind the cargo / hangars, bowsers by the stands
  for (int i = 0; i < (big ? 4 : 2); i++) P.put(EK_FUEL_TANK, L.apU0 + 14.f + i * 15.f, L.bldV + (big ? 80.f : 52.f), 0, -1, big ? 1.f : 0.7f, big ? 1.1f : 0.7f, big ? 1.f : 0.7f);
  for (int i = 0; i < (big ? 2 : 1); i++) P.put(EK_TRUCK, L.standU0 + 12.f + i * 11.f, L.apV1 - 4.5f, 1, 0, 1, 1, 1, false);   // parked along the apron edge, behind the stop bars
  // ---- apron floodlights along the back of the apron (not in front of the gates)
  for (float fu = L.apU0 + 30.f; fu < L.apU1; fu += big ? 90.f : 75.f)
    if (!big || fabsf(fu - L.termU) > L.termHL + 12.f) P.put(EK_FLOODMAST, fu, L.apV1 - 1.f, 0, -1, 1, big ? 1.2f : 0.85f, 1);
  // ---- car parks behind the terminal: double rows of stalls either side of each aisle (16 m modules)
  float cu0 = tU0 - 20.f, cu1 = tU1 + 20.f;
  for (float m = L.lotV0; m + 16.f <= L.lotV1 + 0.1f; m += 16.f) { P.cars(cu0, cu1, m + 2.75f, 1, 0.7f); P.cars(cu0, cu1, m + 13.25f, -1, 0.7f); }
  P.cars(L.tieU0 + 4.f, L.tieU0 + 30.f, L.bldV + 34.f, 1, 0.6f);   // by the FBO / hangars
  // ---- blue taxiway edge lights: both edges of the parallel taxiway every 60 m, and the exits
  auto blue = [&](float u, float vv) { vec3 p = aptWorld(a, u, L.side * vv, 0); P.out.push_back({EK_RWYLIGHT, {p.x, std::max(P.gnd(p.x, p.z), a.elev - 0.5f), p.z, 0, 1, 1, 1, 4.5f}}); };
  float tEnd = hl - 25.f + L.twHW;
  for (float tu = -tEnd; tu <= tEnd; tu += 60.f) {
    bool atExit = false;
    for (int e = 0; e < L.nExit; e++) if (fabsf(tu - L.exitU[e]) < L.twHW + 4.f) atExit = true;
    if (!atExit) blue(tu, L.twV - L.twHW - 1.f);
    bool atApron = tu > L.apU0 && tu < L.apU1;
    if (!atApron) blue(tu, L.twV + L.twHW + 1.f);
  }
  for (int e = 0; e < L.nExit; e++)
    for (float ev = hw + 40.f; ev < L.twV - L.twHW - 5.f; ev += 30.f) for (int sd = -1; sd <= 1; sd += 2) blue(L.exitU[e] + sd * (L.twHW + 1.f), ev);
  // ---- beacon, windsocks, navaids, radar, fence
  P.put(EK_BEACON, tU1 + (big ? 60.f : 26.f) + 16.f, L.bldV + (big ? 52.f : 24.f), 0, -1, 1, 1, 1);
  for (int e = -1; e <= 1; e += 2) P.put(EK_WINDSOCK, e * (hl - 220.f), -(hw + 55.f), 0, 1, 1, 1, 1, false);
  navaids(P, a, L, big);
  if (big) P.put(EK_RADAR, L.apU0 - 160.f, L.bldV + 70.f, 0, -1, 1, 1, 1);
  float fu = hl + 420.f, fv0 = -(hw + 160.f), fv1 = L.lotV1 + 18.f;
  P.fence(-fu, fv0, fu, fv0);
  // Public access is on the landside only. Allow for the end-post radius, keeping
  // the full 16 m entrance clear rather than painting a road through a fence.
  P.fence(-fu, fv1, L.termU - 8.2f, fv1); P.fence(L.termU + 8.2f, fv1, fu, fv1);
  P.fence(-fu, fv0, -fu, fv1); P.fence(fu, fv0, fu, fv1);
}

// strips: a parking area, a couple of hangars, a club house, fuel, windsocks, and the place's own character
void strip(Placer& P, const Airport& a, const AptLayout& L) {
  float hl = a.length * 0.5f, hw = a.width * 0.5f, pu = L.termU;
  std::string code = a.code;
  bool farm = code == "HFS" || code == "ORC", polar = code == "GLS", volcano = code == "VCF", mountain = code == "SMP", beach = code == "PMB";
  // hangars along the back of the parking area
  P.facing(EK_ARCH_HANGAR, pu - 34.f, L.bldV, 1, 1, 1);
  if (code == "GLR") { P.facing(EK_HANGAR, pu + 22.f, L.bldV, 0.6f, 0.55f, 0.6f); P.facing(EK_HANGAR, pu + 52.f, L.bldV, 0.55f, 0.5f, 0.55f); }
  else if (!mountain && !polar && !volcano) P.facing(EK_T_HANGAR, pu + 20.f, L.bldV, 0.5f, 1.f, 1.f);
  // club house / flight office with a few cars beside it
  if (!polar && !volcano) {
    P.facing(beach || mountain ? EK_HOUSE_HIP : EK_FBO, pu + (code == "GLR" ? 88.f : 56.f), L.bldV + 4.f, 0.75f, 0.85f, 0.75f);
    P.cars(pu + 40.f, pu + 68.f, L.bldV + 22.f, -1, 0.5f);
  }
  // fuel pump at the front of the parking area, parked aircraft in it
  P.put(EK_FUEL_PUMP, pu + 42.f, L.apV0 + 13.f, 0, -1, 1, 1, 1);
  int nPlanes = mountain || polar || volcano ? 1 : beach ? 2 : 3;
  for (int i = 0; i < nPlanes; i++) P.put(EK_GA_PLANE, pu - 40.f + i * 15.f + P.rnd(-1.f, 1.f), L.apV0 + 26.f + P.rnd(-2.f, 2.f), P.rnd(-0.1f, 0.1f), -1, 1, 1, 1, false);
  // windsocks: one mid-field opposite the buildings, one by the parking area
  P.put(EK_WINDSOCK, 0, -(hw + 30.f), 0, 1, 1, 1, 1, false);
  P.put(EK_WINDSOCK, pu - 72.f, L.apV0 + 2.f, 0, 1, 1, 1, 1, false);
  // character
  if (farm) {
    P.facing(EK_BARN, pu + 130.f, L.bldV + 10.f, 1, 1, 1);
    P.put(EK_SILO, pu + 152.f, L.bldV + 30.f, 0, -1, 1, 1, 1);
    P.facing(EK_FARMHOUSE, pu + 175.f, L.bldV + 40.f, 1, 1, 1);
    P.put(EK_TRUCK, pu + 112.f, L.bldV + 4.f, 1, 0, 0.8f, 0.8f, 0.8f, false);
  }
  if (mountain) {   // rescue hut and a radio mast on the pass
    P.put(EK_MAST, pu + 90.f, L.bldV + 14.f, 0, -1, 1, 1.2f, 1);
  }
  if (volcano) {   // observatory: lab building, radome, antenna
    P.facing(EK_WAREHOUSE, pu + 40.f, L.bldV, 1.2f, 0.9f, 1.f);
    P.put(EK_RADAR, pu + 85.f, L.bldV + 24.f, 0, -1, 0.65f, 0.65f, 0.65f);
    P.put(EK_MAST, pu + 110.f, L.bldV + 10.f, 0, -1, 1, 1.4f, 1);
    P.put(EK_TRUCK, pu + 14.f, L.bldV - 6.f, 1, 0, 0.8f, 0.8f, 0.8f, false);
  }
  if (polar) {   // research station: insulated blocks, fuel tanks, weather radome, masts
    for (int i = 0; i < 3; i++) P.facing(EK_WAREHOUSE, pu + 30.f + i * 34.f, L.bldV + (i & 1) * 12.f, 1.1f, 0.8f, 0.9f);
    for (int i = 0; i < 2; i++) P.put(EK_FUEL_TANK, pu - 70.f - i * 13.f, L.bldV + 14.f, 0, -1, 0.6f, 0.6f, 0.6f);
    P.put(EK_RADAR, pu + 140.f, L.bldV + 30.f, 0, -1, 0.6f, 0.6f, 0.6f);
    for (int i = 0; i < 2; i++) P.put(EK_MAST, pu + 120.f + i * 40.f, L.bldV + 60.f, 0, -1, 1, 1.6f, 1);
    P.put(EK_TRUCK, pu + 20.f, L.apV1 - 6.f, 1, 0, 1, 1, 1, false);
  }
  if (code == "GLR") P.put(EK_BEACON, pu + 110.f, L.bldV + 6.f, 0, -1, 1, 0.8f, 1);
  (void)hl;
}
// Landside service yards and inhabited field edges, all on the existing airport ground.
// A separate random stream means extending this pass cannot reshuffle aircraft or gate assignments.
void groundLife(Placer& P, const Airport& a, const AptLayout& L) {
  if (L.paved) {
    bool big = a.size == 2;
    // Low logistics sheds behind the hangar/cargo line, with parked service vehicles
    // beside their loading doors. Leave the emergency apron edge entirely unobstructed.
    float yardU = L.apU0 + (big ? 106.f : 68.f);
    float yardV = L.bldV + (big ? 58.f : 45.f);
    P.detail(EK_WAREHOUSE, yardU, yardV, 0, -1, big ? .95f : .6f, big ? .7f : .5f, .65f);
    for (int i = 0; i < (big ? 3 : 2); i++)
      P.detail(EK_TRUCK, yardU + (big ? 18.f : 12.f) + i * 5.f, yardV - 3.f, 0, -1, .85f, .85f, .85f);
    // Staff parking is distinct from the terminal passenger lot.
    for (int i = 0; i < (big ? 9 : 5); i++)
      if (P.rnd() < .8f) P.detail(EK_CAR, L.apU0 + 16.f + i * 3.2f, L.bldV + (big ? 45.f : 29.f), 0, 1);
    // A small operations office at the maintenance end, and its duty vehicle.
    float opsU = L.apU1 + (big ? 34.f : 30.f);
    P.detail(EK_FBO, opsU, L.bldV + 25.f, 0, -1, .7f, .7f, .7f);
    P.detail(EK_TRUCK, opsU + 13.f, L.bldV + 20.f, 0, -1, .8f, .8f, .8f);
    P.detail(EK_CAR, opsU - 13.f, L.bldV + 20.f, 0, -1);
    // Human-scale car-park lighting, scaled from the existing floodlight fixture. The
    // entry itself stays clear for a community road and emergency/service access.
    for (int side = -1; side <= 1; side += 2) {
      float edgeU = L.termU + side * (L.termHL + 27.f);
      P.detail(EK_FLOODMAST, edgeU, L.lotV0 + 8.f, 0, -1, .48f, .38f, .48f);
      P.detail(EK_FLOODMAST, edgeU, L.lotV1 - 8.f, 0, -1, .48f, .38f, .48f);
      // Low planting at the outer boundary frames the parking area without masking
      // the terminal, blocking the aisle or introducing trees close to flying space.
      for (int i = 0; i < 3; i++)
        P.detail(EK_BUSH, L.termU + side * (14.f + i * 7.f), L.lotV1 + 7.f, 0, -1, .85f, .7f, .85f);
    }
    // Fence-backed fuel delivery bays: trucks remain outside the tank footprints.
    P.detail(EK_TRUCK, L.apU0 + (big ? 78.f : 45.f), L.bldV + (big ? 80.f : 52.f), 0, 1, .85f, .85f, .85f);
  } else {
    const bool polar = !strcmp(a.code, "GLS"), volcano = !strcmp(a.code, "VCF");
    const bool farm = !strcmp(a.code, "HFS") || !strcmp(a.code, "ORC");
    float officeU = L.termU + (!strcmp(a.code, "GLR") ? 88.f : 56.f);
    // Flight clubs and research outposts each get an organised, sparse landside yard.
    // Vehicles are stationary; there is no pedestrian/vehicle animation infrastructure.
    P.detail(EK_TRUCK, L.termU - 56.f, L.bldV + 19.f, 0, -1, .8f, .8f, .8f);
    P.detail(EK_WAREHOUSE, L.termU - 56.f, L.bldV + 36.f, 0, -1, .45f, .4f, .5f);
    if (!polar && !volcano) {
      for (int i = 0; i < 4; i++)
        P.detail(EK_CAR, officeU - 7.f + i * 3.3f, L.bldV + 33.f, 0, -1);
      P.detail(EK_FLOODMAST, officeU - 18.f, L.bldV + 28.f, 0, -1, .35f, .25f, .35f);
      if (farm) P.detail(EK_TRUCK, L.termU + 116.f, L.bldV + 32.f, 1, 0, .8f, .8f, .8f);
      // Low hedges mark the public edge, avoiding the existing strip and parked planes.
      for (int i = 0; i < 3; i++)
        P.detail(EK_BUSH, L.termU - 25.f + i * 9.f, L.bldV + 39.f, 0, -1, .8f, .7f, .8f);
    } else {
      P.detail(EK_FLOODMAST, L.termU - 47.f, L.bldV + 24.f, 0, -1, .45f, .4f, .45f);
      P.detail(EK_TRUCK, L.termU - 14.f, L.bldV + 39.f, 0, -1, .85f, .85f, .85f);
    }
  }
}
}  // namespace

void airportItems(int ai, std::vector<AptItem>& out) {
  out.clear();
  const Airport& a = g_world.airports[ai];
  AptLayout L = aptLayout(a, ai);
  Placer P{a, L, out, 0x9E3779B9u ^ (uint32_t)(ai * 2654435761u)};
  if (L.paved) paved(P, a, L); else strip(P, a, L);
  P.rs = 0xA17F93C5u ^ (uint32_t)(ai * 747796405u);
  groundLife(P, a, L);
}
