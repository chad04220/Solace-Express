// Solace Express - procedural meshes for trees, rock formations and buildings
#include "entity_mesh.h"
#include <map>

namespace {
// ------------------------------------------------------------------ noise
float hash3i(int x, int y, int z) {
  uint32_t hx = uint32_t(x)*73856093u ^ (uint32_t(z)*19349663u);
  uint32_t hy = uint32_t(y)*83492791u + uint32_t(z)*2971u;
  return hash2i(static_cast<int32_t>(hx), static_cast<int32_t>(hy));
}
float vnoise3(vec3 p) {
  int ix = (int)floorf(p.x), iy = (int)floorf(p.y), iz = (int)floorf(p.z);
  float fx = p.x - ix, fy = p.y - iy, fz = p.z - iz;
  fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy); fz = fz * fz * (3 - 2 * fz);
  auto h = [&](int a, int b, int c) { return hash3i(ix + a, iy + b, iz + c); };
  float x00 = lerpf(h(0, 0, 0), h(1, 0, 0), fx), x10 = lerpf(h(0, 1, 0), h(1, 1, 0), fx);
  float x01 = lerpf(h(0, 0, 1), h(1, 0, 1), fx), x11 = lerpf(h(0, 1, 1), h(1, 1, 1), fx);
  return lerpf(lerpf(x00, x10, fy), lerpf(x01, x11, fy), fz);
}
float fbm3(vec3 p, int oct) { float s = 0, a = 0.5f, n = 0; for (int i = 0; i < oct; i++) { s += a * vnoise3(p); n += a; p = p * 2.03f + vec3(5.1f, -3.7f, 9.3f); a *= 0.5f; } return s / n; }

// ------------------------------------------------------------------ mesh builder
struct MB {
  std::vector<EVert>& o;
  int wheelSlot = 0;
  explicit MB(std::vector<EVert>& v) : o(v) {}
  void vert(vec3 p, vec3 n, int part, float ao = 1, float u = 0, float v = 0) { o.push_back({p.x, p.y, p.z, n.x, n.y, n.z, (float)part, ao, u, v}); }
  // flat triangle; the normal is flipped to face away from ref (a point inside the solid)
  void tri(vec3 a, vec3 b, vec3 c, int part, vec3 ref, float ao = 1) {
    vec3 n = normalize(cross(b - a, c - a));
    if (dot(n, (a + b + c) * (1.f / 3.f) - ref) < 0) n = -n;
    vert(a, n, part, ao); vert(b, n, part, ao); vert(c, n, part, ao);
  }
  void quad(vec3 a, vec3 b, vec3 c, vec3 d, int part, vec3 ref, float ao = 1) { tri(a, b, c, part, ref, ao); tri(a, c, d, part, ref, ao); }
  // axis-aligned box; faces bitmask: 1 -x, 2 +x, 4 -y, 8 +y, 16 -z, 32 +z
  void box(vec3 mn, vec3 mx, int part, int faces = 0x3B, float ao = 1) {
    vec3 c = (mn + mx) * 0.5f;
    float x0 = mn.x, y0 = mn.y, z0 = mn.z, x1 = mx.x, y1 = mx.y, z1 = mx.z;
    if (faces & 1) quad({x0, y0, z0}, {x0, y0, z1}, {x0, y1, z1}, {x0, y1, z0}, part, c, ao);
    if (faces & 2) quad({x1, y0, z0}, {x1, y1, z0}, {x1, y1, z1}, {x1, y0, z1}, part, c, ao);
    if (faces & 4) quad({x0, y0, z0}, {x1, y0, z0}, {x1, y0, z1}, {x0, y0, z1}, part, c, ao);
    if (faces & 8) quad({x0, y1, z0}, {x0, y1, z1}, {x1, y1, z1}, {x1, y1, z0}, part, c, ao);
    if (faces & 16) quad({x0, y0, z0}, {x0, y1, z0}, {x1, y1, z0}, {x1, y0, z0}, part, c, ao);
    if (faces & 32) quad({x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1}, part, c, ao);
  }
  // gable roof over [x0,x1] x [z0,z1] (walls), overhang ov, eave height e, ridge height r; ridge along z (alongZ) or x.
  // Also closes the gable ends of the walls (wallPart).
  void gable(float x0, float x1, float z0, float z1, float e, float r, bool alongZ, float ov, int roofPart, int wallPart) {
    vec3 ref((x0 + x1) * 0.5f, e - 1.f, (z0 + z1) * 0.5f);
    if (alongZ) {
      float xm = (x0 + x1) * 0.5f, hw = (x1 - x0) * 0.5f + ov, k = (r - e) / ((x1 - x0) * 0.5f);
      float eo = e - ov * k;
      quad({xm - hw, eo, z0 - ov}, {xm, r, z0 - ov}, {xm, r, z1 + ov}, {xm - hw, eo, z1 + ov}, roofPart, ref);
      quad({xm + hw, eo, z0 - ov}, {xm, r, z0 - ov}, {xm, r, z1 + ov}, {xm + hw, eo, z1 + ov}, roofPart, ref);
      tri({x0, e, z0}, {x1, e, z0}, {xm, r, z0}, wallPart, ref); tri({x0, e, z1}, {x1, e, z1}, {xm, r, z1}, wallPart, ref);
    } else {
      float zm = (z0 + z1) * 0.5f, hd = (z1 - z0) * 0.5f + ov, k = (r - e) / ((z1 - z0) * 0.5f);
      float eo = e - ov * k;
      quad({x0 - ov, eo, zm - hd}, {x0 - ov, r, zm}, {x1 + ov, r, zm}, {x1 + ov, eo, zm - hd}, roofPart, ref);
      quad({x0 - ov, eo, zm + hd}, {x0 - ov, r, zm}, {x1 + ov, r, zm}, {x1 + ov, eo, zm + hd}, roofPart, ref);
      tri({x0, e, z0}, {x0, e, z1}, {x0, r, zm}, wallPart, ref); tri({x1, e, z0}, {x1, e, z1}, {x1, r, zm}, wallPart, ref);
    }
  }
  // hip roof over a rectangle (overhang included in the extents)
  void hip(float x0, float x1, float z0, float z1, float e, float r, int part) {
    float xm = (x0 + x1) * 0.5f, zm = (z0 + z1) * 0.5f, wx = (x1 - x0) * 0.5f, wz = (z1 - z0) * 0.5f;
    vec3 ref(xm, e - 1.f, zm);
    vec3 a(x0, e, z0), b(x1, e, z0), c(x1, e, z1), d(x0, e, z1);
    if (fabsf(wx-wz)<1e-5f) {
      // A square hip has one apex; emitting a zero-length ridge made two degenerate triangles.
      vec3 t(xm,r,zm);tri(a,b,t,part,ref);tri(b,c,t,part,ref);tri(c,d,t,part,ref);tri(d,a,t,part,ref);
    } else if (wx >= wz) {
      vec3 p(xm - (wx - wz), r, zm), q(xm + (wx - wz), r, zm);
      quad(a, b, q, p, part, ref); quad(d, c, q, p, part, ref); tri(a, d, p, part, ref); tri(b, c, q, part, ref);
    } else {
      vec3 p(xm, r, zm - (wz - wx)), q(xm, r, zm + (wz - wx));
      quad(a, d, q, p, part, ref); quad(b, c, q, p, part, ref); tri(a, b, p, part, ref); tri(d, c, q, part, ref);
    }
  }
  // tapered cylinder / cone around a vertical axis (or between two points), smooth sides
  void cyl(vec3 c0, vec3 c1, float r0, float r1, int segs, int part, bool capTop, bool capBot, float ao0 = 1, float ao1 = 1, float rot = 0, bool smooth = true) {
    vec3 ax = normalize(c1 - c0);
    vec3 t = normalize(cross(ax, fabsf(ax.y) < 0.9f ? vec3(0, 1, 0) : vec3(1, 0, 0))), b = cross(ax, t);
    float len = length(c1 - c0), slope = (r0 - r1) / std::max(len, 1e-3f);
    for (int i = 0; i < segs; i++) {
      float a0 = rot + i * 2 * PI / segs, a1 = rot + (i + 1) * 2 * PI / segs;
      vec3 d0 = t * cosf(a0) + b * sinf(a0), d1 = t * cosf(a1) + b * sinf(a1);
      vec3 p00 = c0 + d0 * r0, p01 = c0 + d1 * r0, p10 = c1 + d0 * r1, p11 = c1 + d1 * r1;
      if (smooth) {
        vec3 n0 = normalize(d0 + ax * slope), n1 = normalize(d1 + ax * slope);
        vert(p00, n0, part, ao0); vert(p01, n1, part, ao0); vert(p11, n1, part, ao1);
        if (r1 > 1e-4f) { vert(p00, n0, part, ao0); vert(p11, n1, part, ao1); vert(p10, n0, part, ao1); }
      } else {
        vec3 ref = (c0 + c1) * 0.5f;
        if (r1 > 1e-4f) quad(p00, p01, p11, p10, part, ref, ao0); else tri(p00, p01, c1, part, ref, ao0);
      }
      if (capTop && r1 > 1e-4f) { vert(c1, ax, part, ao1); vert(p10, ax, part, ao1); vert(p11, ax, part, ao1); }
      if (capBot) { vert(c0, -ax, part, ao0); vert(p01, -ax, part, ao0); vert(p00, -ax, part, ao0); }
    }
  }
};

// ------------------------------------------------------------------ icosphere
struct IcoMesh { std::vector<vec3> v; std::vector<int> t; };
const IcoMesh& icosphere(int sub) {
  static std::map<int, IcoMesh> cache;
  auto it = cache.find(sub);
  if (it != cache.end()) return it->second;
  IcoMesh m;
  float g = (1.f + sqrtf(5.f)) * 0.5f;
  vec3 v0[12] = {{-1, g, 0}, {1, g, 0}, {-1, -g, 0}, {1, -g, 0}, {0, -1, g}, {0, 1, g}, {0, -1, -g}, {0, 1, -g}, {g, 0, -1}, {g, 0, 1}, {-g, 0, -1}, {-g, 0, 1}};
  int t0[60] = {0, 11, 5, 0, 5, 1, 0, 1, 7, 0, 7, 10, 0, 10, 11, 1, 5, 9, 5, 11, 4, 11, 10, 2, 10, 7, 6, 7, 1, 8,
                3, 9, 4, 3, 4, 2, 3, 2, 6, 3, 6, 8, 3, 8, 9, 4, 9, 5, 2, 4, 11, 6, 2, 10, 8, 6, 7, 9, 8, 1};
  for (auto& p : v0) m.v.push_back(normalize(p));
  m.t.assign(t0, t0 + 60);
  for (int s = 0; s < sub; s++) {
    std::map<std::pair<int, int>, int> mid;
    auto midp = [&](int a, int b) {
      auto k = std::make_pair(std::min(a, b), std::max(a, b));
      auto f = mid.find(k);
      if (f != mid.end()) return f->second;
      m.v.push_back(normalize(m.v[a] + m.v[b]));
      return mid[k] = (int)m.v.size() - 1;
    };
    std::vector<int> nt;
    for (size_t i = 0; i < m.t.size(); i += 3) {
      int a = m.t[i], b = m.t[i + 1], c = m.t[i + 2], ab = midp(a, b), bc = midp(b, c), ca = midp(c, a);
      int q[12] = {a, ab, ca, b, bc, ab, c, ca, bc, ab, bc, ca};
      nt.insert(nt.end(), q, q + 12);
    }
    m.t.swap(nt);
  }
  return cache[sub] = m;
}

// Displaced sphere. pos(dir) gives the surface point for a unit direction; normals are smooth (averaged), or
// bent towards `bendC` (foliage: a canopy-wide normal reads as one soft crown instead of many lumps).
template <class F, class A>
void blob(MB& mb, int sub, F pos, A aoFn, int part, bool flat = false, const vec3* bendC = nullptr, float bend = 0, float tag = 0, float facet = 0) {
  const IcoMesh& ico = icosphere(sub);
  std::vector<vec3> P(ico.v.size()), N(ico.v.size(), vec3(0, 0, 0));
  for (size_t i = 0; i < ico.v.size(); i++) P[i] = pos(ico.v[i]);
  for (size_t i = 0; i < ico.t.size(); i += 3) {
    int a = ico.t[i], b = ico.t[i + 1], c = ico.t[i + 2];
    vec3 n = cross(P[b] - P[a], P[c] - P[a]);
    N[a] += n; N[b] += n; N[c] += n;
  }
  for (size_t i = 0; i < ico.t.size(); i += 3) {
    int id[3] = {ico.t[i], ico.t[i + 1], ico.t[i + 2]};
    vec3 fn = normalize(cross(P[id[1]] - P[id[0]], P[id[2]] - P[id[0]]));
    for (int k = 0; k < 3; k++) {
      vec3 n = flat ? fn : normalize(N[id[k]]);
      if (facet > 0.f) n = normalize(n * (1.f - facet) + fn * facet);
      if (bendC) n = normalize(n * (1.f - bend) + normalize(P[id[k]] - *bendC) * bend);
      mb.vert(P[id[k]], n, part, aoFn(P[id[k]], ico.v[id[k]]), tag);
    }
  }
}

// ------------------------------------------------------------------ trees
// foliage clump: a lumpy ellipsoid; ao darkens the inside and the bottom of the crown
void clump(MB& mb, vec3 c, float r, float squash, int sub, int part, vec3 crownC, float yBot, float yTop, float seed, float lump = 0.22f) {
  blob(mb, sub,
       [&](vec3 d) { float az = atan2f(d.z, d.x), phase = seed * 9.73f;
         float lobe = sinf(az * 3.f + phase) * (1.f - d.y * d.y);
         float k = 1.f + lump * (0.6f * (fbm3(d * 2.1f + vec3(seed * 13.f, seed * 7.f, seed * 3.f), 2) - 0.5f) * 2.f + 0.4f * lobe);
         // Broad crown lobes and a slightly wind-shaped lower skirt, stable at every subdivision.
         return c + vec3(d.x * r * k, d.y * r * k * squash, d.z * r * k * (0.92f + 0.08f * cosf(phase))); },
       [&](vec3 p, vec3) { float h = clampf((p.y - yBot) / std::max(yTop - yBot, 0.1f), 0, 1); float out = clampf(length(p - crownC) / std::max(r * 1.6f, 0.5f), 0, 1);
         return clampf(0.3f + 0.42f * h + 0.35f * out * out, 0.22f, 1.f); },
       part, false, &crownC, 0.4f, fmodf(seed * 7.77f, 1.f));
}

// leaf cards: small alpha-cut quads of procedural leaf clusters filling a crown clump (nearest detail level only)
void leafCards(MB& mb, vec3 c, float r, float squash, int n, vec3 crownC, float yBot, float yTop, float seed, float size = 0.75f) {
  for (int k = 0; k < n; k++) {
    float h1 = hash2i(k * 7 + 1, (int)(seed * 9973)), h2v = hash2i(k * 3 + 5, (int)(seed * 7919) + 11), h3 = hash2i(k * 11 + 2, (int)(seed * 6007) + 3);
    float th = h1 * 2 * PI, ph = acosf(1.f - 2.f * h2v);
    vec3 d(sinf(ph) * cosf(th), cosf(ph), sinf(ph) * sinf(th));
    float rr = r * (0.62f + 0.38f * sqrtf(h3));
    vec3 p = c + vec3(d.x * rr, d.y * rr * squash, d.z * rr);
    vec3 nrm = normalize(d + vec3(hash2i(k, 3) - 0.5f, hash2i(k, 4) - 0.5f, hash2i(k, 5) - 0.5f) * 1.2f);
    vec3 t = normalize(cross(nrm, fabsf(nrm.y) < 0.9f ? vec3(0, 1, 0) : vec3(1, 0, 0))), b = cross(nrm, t);
    float sz = r * size * 0.70f * (0.8f + 0.4f * hash2i(k, 9));
    vec3 ln = normalize(normalize(p - crownC) * 0.65f + d * 0.35f);   // lit like part of the crown
    float hgt = clampf((p.y - yBot) / std::max(yTop - yBot, 0.1f), 0, 1), out = clampf(length(p - crownC) / std::max(r * 1.6f, 0.5f), 0, 1);
    float ao = clampf(0.3f + 0.42f * hgt + 0.35f * out * out, 0.22f, 1.f);
    vec3 q[4] = {p - t * sz - b * sz, p + t * sz - b * sz, p + t * sz + b * sz, p - t * sz + b * sz};
    float uv[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    int id[6] = {0, 1, 2, 0, 2, 3};
    for (int i : id) mb.vert(q[i], ln, P_LEAFCARD, ao, uv[i][0] + (float)(k % 8), uv[i][1]);
  }
}

void trunk(MB& mb, vec3 a, vec3 b, float r0, float r1, int segs, float ao0 = 0.7f, float ao1 = 0.9f) { mb.cyl(a, b, r0, r1, segs, P_BARK, false, false, ao0, ao1); }

// Conifers are radial boughs, not a stack of continuous conical skirts. Each bough
// uses a pointed diamond of needles and a shallow underside; the space between tips
// shows real branch gaps. The original 396/540-triangle near budgets are unchanged.
#include "entity_mesh_nature.inc"
#include "entity_mesh_buildings.inc"
#include "entity_mesh_airport.inc"
}  // namespace

void buildEntityMeshes(std::vector<EVert>& out, EntMeshRange ranges[EK_COUNT]) {
  out.clear();
  MB mb(out);
  for (int k = 0; k < EK_COUNT; k++)
    for (int l = 0; l < ENT_LODS; l++) {
      mb.wheelSlot = 0;
      ranges[k].first[l] = (int)out.size();
      int cls = entClass(k);
      if (k >= EK_HANGAR) { buildAirportKind(mb, k, l); airportAccents(mb, k, l); }
      else if (cls == EC_TREE) buildTree(mb, k, l);
      else if (cls == EC_ROCK) buildRock(mb, k, l);
      else { buildBuilding(mb, k, l); buildingAccents(mb, k, l); }
      ranges[k].count[l] = (int)out.size() - ranges[k].first[l];
    }
}
