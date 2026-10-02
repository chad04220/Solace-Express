// Air Xpress - procedural meshes for trees, rock formations and buildings
#include "entity_mesh.h"
#include <map>

namespace {
// ------------------------------------------------------------------ noise
float hash3i(int x, int y, int z) { return hash2i(x * 73856093 ^ (z * 19349663), y * 83492791 + z * 2971); }
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
    if (wx >= wz) {
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
void blob(MB& mb, int sub, F pos, A aoFn, int part, bool flat = false, const vec3* bendC = nullptr, float bend = 0, float tag = 0) {
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
      if (bendC) n = normalize(n * (1.f - bend) + normalize(P[id[k]] - *bendC) * bend);
      mb.vert(P[id[k]], n, part, aoFn(P[id[k]], ico.v[id[k]]), tag);
    }
  }
}

// ------------------------------------------------------------------ trees
// foliage clump: a lumpy ellipsoid; ao darkens the inside and the bottom of the crown
void clump(MB& mb, vec3 c, float r, float squash, int sub, int part, vec3 crownC, float yBot, float yTop, float seed, float lump = 0.22f) {
  blob(mb, sub,
       [&](vec3 d) { float k = 1.f + lump * (fbm3(d * 2.1f + vec3(seed * 13.f, seed * 7.f, seed * 3.f), 2) - 0.5f) * 2.f; return c + vec3(d.x * r * k, d.y * r * k * squash, d.z * r * k); },
       [&](vec3 p, vec3) { float h = clampf((p.y - yBot) / std::max(yTop - yBot, 0.1f), 0, 1); float out = clampf(length(p - crownC) / std::max(r * 1.6f, 0.5f), 0, 1);
         return clampf(0.3f + 0.42f * h + 0.35f * out * out, 0.22f, 1.f); },
       part, false, &crownC, 0.55f, fmodf(seed * 7.77f, 1.f));
}

// leaf cards: small alpha-cut quads of procedural leaf clusters filling a crown clump (nearest detail level only)
void leafCards(MB& mb, vec3 c, float r, float squash, int n, vec3 crownC, float yBot, float yTop, float seed, float size = 0.75f) {
  for (int k = 0; k < n; k++) {
    float h1 = hash2i(k * 7 + 1, (int)(seed * 9973)), h2v = hash2i(k * 3 + 5, (int)(seed * 7919) + 11), h3 = hash2i(k * 11 + 2, (int)(seed * 6007) + 3);
    float th = h1 * 2 * PI, ph = acosf(1.f - 2.f * h2v);
    vec3 d(sinf(ph) * cosf(th), cosf(ph), sinf(ph) * sinf(th));
    float rr = r * (0.45f + 0.55f * sqrtf(h3));
    vec3 p = c + vec3(d.x * rr, d.y * rr * squash, d.z * rr);
    vec3 nrm = normalize(d + vec3(hash2i(k, 3) - 0.5f, hash2i(k, 4) - 0.5f, hash2i(k, 5) - 0.5f) * 1.2f);
    vec3 t = normalize(cross(nrm, fabsf(nrm.y) < 0.9f ? vec3(0, 1, 0) : vec3(1, 0, 0))), b = cross(nrm, t);
    float sz = r * size * 0.85f * (0.8f + 0.4f * hash2i(k, 9));
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

// conifer: stacked jagged cones
void conifer(MB& mb, int lod, float H, float R, float yBase, int tiers, float taper, float seed) {
  if (lod >= 2) {   // one cone
    mb.cyl(vec3(0, yBase * 0.6f, 0), vec3(0, H, 0), R * 0.95f, 0.f, 6, P_NEEDLE, false, true, 0.8f, 0.95f);
    return;
  }
  trunk(mb, vec3(0, -1.f, 0), vec3(0, H * 0.75f, 0), R * 0.1f + 0.08f, 0.05f, lod == 0 ? 6 : 4);
  int segs = lod == 0 ? 9 : 6;
  int n = lod == 0 ? tiers : std::max(3, tiers / 2);
  for (int i = 0; i < n; i++) {
    float t = (float)i / (n - 1);
    float th = (H - yBase) / n * (lod == 0 ? 1.9f : 2.3f);
    float y0 = yBase + (H - yBase - th * 0.9f) * powf(t, 0.92f);
    float r = R * (1.f - t * taper) * (0.92f + 0.12f * hash2i(i * 7 + (int)(seed * 100), 3));
    vec3 apex(0.f, std::min(y0 + th, H), 0.f);
    std::vector<vec3> rim(segs);
    std::vector<float> rr(segs);
    for (int s = 0; s < segs; s++) {
      float a = (s + 0.5f * (i & 1)) * 2 * PI / segs + seed;
      float hr = hash2i(i * 31 + s, (int)(seed * 977) + 5);
      rr[s] = r * (0.78f + 0.36f * hr);
      rim[s] = vec3(cosf(a) * rr[s], y0 - rr[s] * (0.18f + 0.22f * hr), sinf(a) * rr[s]);
    }
    float aoT = lerpf(0.62f, 1.f, t);
    for (int s = 0; s < segs; s++) {
      vec3 a = rim[s], b = rim[(s + 1) % segs];
      float slopeA = rr[s] / std::max(apex.y - a.y, 0.3f), slopeB = rr[(s + 1) % segs] / std::max(apex.y - b.y, 0.3f);
      vec3 na = normalize(vec3(a.x, rr[s] * slopeA * 0.9f, a.z)), nb = normalize(vec3(b.x, rr[(s + 1) % segs] * slopeB * 0.9f, b.z));
      mb.vert(a, na, P_NEEDLE, aoT); mb.vert(b, nb, P_NEEDLE, aoT); mb.vert(apex, normalize(na + nb + vec3(0, 1.5f, 0)), P_NEEDLE, aoT * 0.75f);
      // underside: a shallow inverted cone back to the trunk keeps the tier solid from below
      if (lod == 0) { vec3 in(0.f, y0 + th * 0.25f, 0.f); mb.vert(a, vec3(na.x, -0.6f, na.z), P_NEEDLE, aoT * 0.55f); mb.vert(in, vec3(0, -1, 0), P_NEEDLE, 0.35f); mb.vert(b, vec3(nb.x, -0.6f, nb.z), P_NEEDLE, aoT * 0.55f); }
    }
  }
}

void buildTree(MB& mb, int kind, int lod) {
  const EntKindInfo& I = kEntInfo[kind];
  int sub = lod == 0 ? 1 : 0;
  switch (kind) {
    case EK_FIR: conifer(mb, lod, I.h, I.hx, 1.6f, 8, 0.86f, 0.3f); break;
    case EK_SPRUCE: conifer(mb, lod, I.h, I.hx, 0.9f, 11, 0.9f, 1.7f); break;
    case EK_PINE: {
      // Scots-style pine: tall bare trunk, whorls of upturned branches carrying lumpy needle tufts, a rounded top
      vec3 cc(0.3f, I.h * 0.8f, 0.1f);
      auto T = [&](float y) { float t = y / I.h; return vec3(0.35f * t * t, y, 0.1f * t * t); };   // gently leaning stem
      if (lod >= 2) {
        trunk(mb, vec3(0, -1, 0), T(I.h * 0.7f), 0.3f, 0.18f, 3);
        clump(mb, T(I.h * 0.86f), 2.4f, 0.55f, 0, P_NEEDLE, cc, cc.y - 3.f, I.h, 0.2f, 0.3f);
        clump(mb, T(I.h * 0.7f) + vec3(1.4f, 0, 0.6f), 2.0f, 0.5f, 0, P_NEEDLE, cc, cc.y - 3.f, I.h, 0.5f, 0.3f);
        clump(mb, T(I.h * 0.72f) + vec3(-1.2f, 0, -0.8f), 1.9f, 0.5f, 0, P_NEEDLE, cc, cc.y - 3.f, I.h, 0.8f, 0.3f);
        break;
      }
      trunk(mb, vec3(0, -1, 0), T(I.h * 0.97f), 0.36f, 0.08f, lod == 0 ? 7 : 4, 0.8f, 0.9f);
      int whorls = lod == 0 ? 7 : 4, perW = lod == 0 ? 4 : 3;
      for (int w = 0; w < whorls; w++) {
        float t = (float)w / (whorls - 1), y = I.h * lerpf(0.5f, 0.9f, t);
        float len = lerpf(3.3f, 1.3f, t);
        for (int bI = 0; bI < perW; bI++) {
          int id = w * 7 + bI;
          float az = (bI + 0.5f * (w & 1)) * 2 * PI / perW + 1.3f * hash2i(id, 17);
          float up = 0.25f + 0.35f * hash2i(id, 23), L = len * (0.75f + 0.5f * hash2i(id, 29));
          vec3 dir = normalize(vec3(cosf(az), up, sinf(az)));
          vec3 b0 = T(y), b1 = b0 + dir * L;
          if (lod == 0) trunk(mb, b0, b1, 0.1f * (1.f - 0.5f * t) + 0.04f, 0.035f, 3, 0.75f, 0.85f);
          float tr = (0.85f + 0.35f * hash2i(id, 31)) * lerpf(1.05f, 0.85f, t);
          vec3 tip = b1 + vec3(0, 0.25f, 0);
          clump(mb, tip, tr * (lod == 0 ? 1.f : 1.35f), 0.6f, 0, P_NEEDLE, cc, cc.y - 3.f, I.h, 0.07f * id, 0.38f);
          if (lod == 0) {
            clump(mb, b0 + dir * (L * 0.55f) + vec3(0, 0.15f, 0), tr * 0.75f, 0.6f, 0, P_NEEDLE, cc, cc.y - 3.f, I.h, 0.07f * id + 0.3f, 0.38f);
            leafCards(mb, tip, tr * 1.25f, 0.65f, 5, cc, cc.y - 3.f, I.h, 0.07f * id + 0.5f, 0.75f);
          }
        }
      }
      clump(mb, T(I.h * 0.95f), lod == 0 ? 1.2f : 1.5f, 0.75f, 0, P_NEEDLE, cc, cc.y - 3.f, I.h, 0.9f, 0.35f);
      break;
    }
    case EK_OAK: {
      vec3 cc(0, 7.f, 0);
      if (lod >= 2) { clump(mb, cc, I.hx * 0.95f, 0.78f, 0, P_LEAF, cc, 3.f, 11.f, 0.2f); mb.cyl(vec3(0, -1, 0), vec3(0, 4.f, 0), 0.45f, 0.3f, 3, P_BARK, false, false); break; }
      trunk(mb, vec3(0, -1, 0), vec3(0.15f, 3.6f, 0), 0.5f, 0.34f, lod == 0 ? 7 : 4, 0.65f, 0.75f);
      vec3 cs[7] = {vec3(0, 8.4f, 0), vec3(2.8f, 6.6f, 0.4f), vec3(-2.6f, 6.9f, 0.9f), vec3(0.6f, 6.4f, 2.8f), vec3(-0.5f, 6.8f, -2.9f), vec3(2.0f, 7.8f, -1.9f), vec3(-1.9f, 7.9f, -1.5f)};
      float rs[7] = {3.0f, 2.5f, 2.6f, 2.4f, 2.5f, 2.2f, 2.2f};
      for (int i = 0; i < (lod == 0 ? 7 : 5); i++) {
        if (i >= 1 && i <= 4) trunk(mb, vec3(0.1f, 3.3f, 0), cs[i] * 0.75f + vec3(0, 0.6f, 0), 0.24f, 0.12f, lod == 0 ? 5 : 3);
        clump(mb, cs[i], rs[i] * (lod == 0 ? 0.7f : 1.12f), 0.82f, sub, P_LEAF, cc, 3.8f, 11.f, 0.11f * i + 0.05f);
        if (lod == 0) leafCards(mb, cs[i], rs[i] * 1.12f, 0.85f, 26, cc, 3.8f, 11.f, 0.11f * i + 0.05f);
      }
      break;
    }
    case EK_BIRCH: {
      vec3 cc(0, 9.0f, 0);
      if (lod >= 2) { clump(mb, cc, 2.5f, 1.55f, 0, P_LEAF, cc, 5.f, 13.f, 0.4f); mb.cyl(vec3(0, -1, 0), vec3(0, 7.f, 0), 0.2f, 0.12f, 3, P_BARK, false, false); break; }
      trunk(mb, vec3(0, -1, 0), vec3(0.2f, 12.f, -0.1f), 0.2f, 0.06f, lod == 0 ? 6 : 4, 0.85f, 0.95f);
      vec3 cs[9] = {vec3(0.2f, 11.6f, -0.1f), vec3(1.1f, 9.9f, 0.5f), vec3(-1.0f, 9.0f, -0.4f), vec3(0.4f, 7.4f, 1.1f), vec3(-0.3f, 10.4f, -1.1f),
                    vec3(-1.2f, 7.8f, 0.6f), vec3(1.2f, 8.0f, -0.8f), vec3(0.5f, 6.4f, -0.3f), vec3(-0.4f, 12.3f, 0.5f)};
      float rs[9] = {1.2f, 1.35f, 1.4f, 1.3f, 1.25f, 1.2f, 1.25f, 1.05f, 0.9f};
      for (int i = 0; i < (lod == 0 ? 9 : 4); i++) {
        if (lod == 0 && i >= 1 && i <= 7) trunk(mb, vec3(0.1f, cs[i].y - 1.6f, -0.05f), cs[i] * 0.8f + vec3(0, cs[i].y * 0.2f, 0), 0.06f, 0.025f, 3, 0.85f, 0.95f);
        clump(mb, cs[i], rs[i] * (lod == 0 ? 0.66f : 1.45f), 1.2f, sub, P_LEAF, cc, 5.5f, 13.f, 0.6f + 0.13f * i, 0.3f);
        if (lod == 0) leafCards(mb, cs[i], rs[i] * 1.15f, 1.2f, 18, cc, 5.5f, 13.f, 0.6f + 0.13f * i, 0.62f);
      }
      break;
    }
    case EK_BUSH: {
      vec3 cc(0, 0.8f, 0);
      if (lod >= 2) { clump(mb, cc, 1.5f, 0.6f, 0, P_LEAF, cc, -0.2f, 1.8f, 0.7f); break; }
      vec3 cs[4] = {vec3(0, 0.95f, 0), vec3(0.75f, 0.6f, 0.2f), vec3(-0.6f, 0.65f, 0.45f), vec3(0.1f, 0.55f, -0.75f)};
      float rs[4] = {0.95f, 0.75f, 0.8f, 0.75f};
      for (int i = 0; i < (lod == 0 ? 4 : 2); i++) {
        clump(mb, cs[i], rs[i] * (lod == 0 ? 0.8f : 1.25f), 0.85f, sub, P_LEAF, cc, -0.2f, 1.8f, 0.9f + 0.1f * i, 0.3f);
        if (lod == 0) leafCards(mb, cs[i], rs[i] * 1.1f, 0.85f, 12, cc, -0.2f, 1.8f, 0.9f + 0.1f * i, 0.7f);
      }
      break;
    }
    case EK_PALM: {
      // curved trunk
      auto P = [&](float t) { return vec3(1.3f * t * t, I.h * 0.88f * t, 0.25f * t * t); };
      int levels = lod == 0 ? 8 : lod == 1 ? 4 : 2, segs = lod == 0 ? 6 : 4;
      for (int i = 0; i < levels; i++) {
        float t0 = (float)i / levels, t1 = (float)(i + 1) / levels;
        vec3 a = P(t0), b = P(t1);
        if (i == 0) a.y = -1.f;
        mb.cyl(a, b, lerpf(0.3f, 0.2f, t0), lerpf(0.3f, 0.2f, t1), segs, P_BARK, false, false, 0.8f, 0.85f);
      }
      vec3 crown = P(1.f);
      int nf = lod == 0 ? 11 : lod == 1 ? 7 : 5, ns = lod == 0 ? 7 : lod == 1 ? 4 : 2;
      for (int f = 0; f < nf; f++) {
        float a = f * 2.399963f + 0.3f;   // golden angle: no two fronds line up
        vec3 dir(cosf(a), 0, sinf(a)), side(-sinf(a), 0, cosf(a));
        float len = 4.4f * (0.85f + 0.3f * hash2i(f, 77)), lift = 0.9f + 0.9f * hash2i(f, 13);
        auto F = [&](float s) { return crown + dir * (len * s) + vec3(0, lift * s - 3.2f * s * s, 0); };
        for (int k = 0; k < ns; k++) {
          float s0 = (float)k / ns, s1 = (float)(k + 1) / ns;
          float w0 = 0.12f + 0.75f * powf(sinf(PI * std::min(s0 * 1.1f, 1.f)), 0.7f), w1 = 0.12f + 0.75f * powf(sinf(PI * std::min(s1 * 1.1f, 1.f)), 0.7f);
          vec3 c0 = F(s0), c1 = F(s1);
          vec3 tg = normalize(c1 - c0);
          // V-shaped cross-section: the leaflets droop to both sides of the midrib
          vec3 l0 = c0 - side * w0 - vec3(0, 0.32f * w0, 0), r0 = c0 + side * w0 - vec3(0, 0.32f * w0, 0);
          vec3 l1 = c1 - side * w1 - vec3(0, 0.32f * w1, 0), r1 = c1 + side * w1 - vec3(0, 0.32f * w1, 0);
          vec3 n = normalize(cross(side, tg)); if (n.y < 0) n = -n;
          float ao0 = 0.7f + 0.3f * s0, ao1 = 0.7f + 0.3f * s1;
          mb.vert(l0, n, P_FROND, ao0, -1, s0); mb.vert(r0, n, P_FROND, ao0, 1, s0); mb.vert(r1, n, P_FROND, ao1, 1, s1);
          mb.vert(l0, n, P_FROND, ao0, -1, s0); mb.vert(r1, n, P_FROND, ao1, 1, s1); mb.vert(l1, n, P_FROND, ao1, -1, s1);
        }
      }
      if (lod == 0) for (int c = 0; c < 4; c++) { float a = c * 1.7f; vec3 q = crown + vec3(cosf(a) * 0.35f, -0.35f, sinf(a) * 0.35f);
        blob(mb, 0, [&](vec3 d) { return q + d * 0.2f; }, [](vec3, vec3) { return 0.7f; }, P_BARK); }
      break;
    }
  }
}

// ------------------------------------------------------------------ rocks
// lumpy rock body: scaled sphere pushed towards a rounded box (cube), with noise and optional strata ledges
void rockBlob(MB& mb, int sub, vec3 c, vec3 s, float yaw, float tilt, float seed, float noiseAmp, float cube, float strata = 0, float topCut = 1e9f) {
  float cy = cosf(yaw), sy = sinf(yaw), ct = cosf(tilt), st = sinf(tilt);
  blob(mb, sub,
       [&](vec3 d) {
         float m = powf(powf(fabsf(d.x), 4) + powf(fabsf(d.y), 4) + powf(fabsf(d.z), 4), 0.25f);
         vec3 q = d * lerpf(1.f, 1.f / std::max(m, 0.3f), cube);
         float n = fbm3(d * 1.9f + vec3(seed * 11.f, seed * 5.f, -seed * 3.f), 3) - 0.5f;
         float k = 1.f + noiseAmp * n * 2.f;
         vec3 p(q.x * s.x * k, q.y * s.y * k, q.z * s.z * k);
         if (strata > 0) { float l = sinf(p.y * strata + seed * 9.f); p.x *= 1.f + 0.05f * l; p.z *= 1.f + 0.05f * l; }
         p = vec3(p.x, p.y * ct - p.z * st, p.y * st + p.z * ct);
         p = vec3(cy * p.x + sy * p.z, p.y, -sy * p.x + cy * p.z);
         p = p + c;
         if (p.y > topCut) p.y = topCut + (p.y - topCut) * 0.08f;
         return p;
       },
       [&](vec3 p, vec3 d) { return clampf(0.62f + 0.38f * (d.y * 0.5f + 0.5f), 0.4f, 1.f); }, P_ROCK);
}

// cliff column: rings of noisy radius with stratified ledges, steep sides and a rough flat top
void rockColumn(MB& mb, vec3 base, int segs, int rings, float H, float R0, float R1, float seed, float strata, float notch, float lean) {
  std::vector<vec3> P((size_t)(rings + 1) * segs);
  for (int j = 0; j <= rings; j++) {
    float t = (float)j / rings, y = H * t;
    float band = 1.f + strata * (hash2i(j, (int)(seed * 1000)) - 0.5f) * 2.f;   // each stratum sticks out or recedes
    for (int i = 0; i < segs; i++) {
      float a = i * 2 * PI / segs;
      float n = fbm3(vec3(cosf(a) * 1.4f + seed * 7.f, t * 2.2f, sinf(a) * 1.4f), 3) - 0.5f;
      float n2 = fbm3(vec3(cosf(a) * 3.7f - seed * 3.f, t * 7.f + j * 0.37f, sinf(a) * 3.7f), 2) - 0.5f;   // blocky fractures
      float r = lerpf(R0, R1, t) * (1.f + 1.15f * n + 0.55f * n2) * band;
      if (t < 0.12f) r *= 1.f - notch * (1.f - t / 0.12f);   // wave-cut notch
      P[(size_t)j * segs + i] = base + vec3(cosf(a) * r + lean * t * t * H * 0.1f, y, sinf(a) * r * 0.86f);
    }
  }
  vec3 axis = base + vec3(0, H * 0.5f, 0);
  for (int j = 0; j < rings; j++)
    for (int i = 0; i < segs; i++) {
      vec3 a = P[(size_t)j * segs + i], b = P[(size_t)j * segs + (i + 1) % segs], c = P[(size_t)(j + 1) * segs + (i + 1) % segs], d = P[(size_t)(j + 1) * segs + i];
      vec3 ref(axis.x, (a.y + c.y) * 0.5f, axis.z);
      float ao = clampf(0.55f + 0.45f * (float)j / rings, 0.5f, 1.f);
      mb.quad(a, b, c, d, P_ROCK, ref, ao);
    }
  // top: a fan with a slight rough dome
  vec3 top(0, 0, 0);
  for (int i = 0; i < segs; i++) top = top + P[(size_t)rings * segs + i];
  top = top * (1.f / segs) + vec3(0, R1 * 0.18f, 0);
  for (int i = 0; i < segs; i++) mb.tri(P[(size_t)rings * segs + i], P[(size_t)rings * segs + (i + 1) % segs], top, P_ROCK, top - vec3(0, 2.f, 0), 1.f);
}

void buildRock(MB& mb, int kind, int lod) {
  int sub = lod == 0 ? 2 : lod == 1 ? 1 : 0;
  switch (kind) {
    case EK_BOULDER: rockBlob(mb, sub, vec3(0, 0.62f, 0), vec3(1.f, 0.78f, 0.92f), 0.3f, 0.1f, 0.21f, 0.32f, 0.15f); break;
    case EK_BLOCK: rockBlob(mb, sub, vec3(0, 0.6f, 0), vec3(1.05f, 0.72f, 0.88f), 1.1f, 0.12f, 0.57f, 0.2f, 0.75f); break;
    case EK_SLAB: rockBlob(mb, sub, vec3(0, 0.3f, 0), vec3(1.55f, 0.42f, 1.15f), 0.2f, 0.16f, 0.83f, 0.16f, 0.85f, 6.f); break;
    case EK_OUTCROP: {
      struct B { vec3 c, s; float yaw, tilt; } bl[7] = {
        {{0, 2.4f, 0}, {4.2f, 3.1f, 3.6f}, 0.2f, 0.05f}, {{3.6f, 1.6f, 1.4f}, {2.6f, 2.2f, 2.5f}, 0.9f, 0.18f},
        {{-3.8f, 1.5f, -1.0f}, {3.0f, 2.0f, 2.4f}, -0.4f, -0.12f}, {{0.8f, 5.6f, -0.4f}, {2.7f, 2.4f, 2.4f}, 1.4f, 0.1f},
        {{-1.6f, 4.9f, 1.3f}, {2.1f, 2.0f, 1.8f}, 0.7f, -0.2f}, {{4.6f, 0.7f, -2.8f}, {1.7f, 1.2f, 1.5f}, 2.2f, 0.3f},
        {{-5.6f, 0.5f, 2.4f}, {1.4f, 1.0f, 1.3f}, 0.1f, 0.25f}};
      int n = lod == 0 ? 7 : lod == 1 ? 5 : 3;
      for (int i = 0; i < n; i++) rockBlob(mb, lod == 0 ? 1 : 0, bl[i].c, bl[i].s, bl[i].yaw, bl[i].tilt, 0.13f * i + 0.2f, 0.2f, 0.7f, i < 3 ? 2.2f : 0.f);
      break;
    }
    case EK_SPIRE: {
      int segs = lod == 0 ? 14 : lod == 1 ? 9 : 6, rings = lod == 0 ? 14 : lod == 1 ? 7 : 3;
      rockColumn(mb, vec3(0, -1.f, 0), segs, rings, 22.5f, 4.2f, 1.9f, 0.37f, 0.16f, 0.f, 0.4f);
      if (lod <= 1) rockBlob(mb, lod == 0 ? 1 : 0, vec3(0.5f, 22.4f, 0.1f), vec3(3.1f, 1.3f, 2.6f), 0.4f, 0.08f, 0.61f, 0.18f, 0.75f);   // cap stone
      if (lod == 0) for (int i = 0; i < 3; i++) rockBlob(mb, 0, vec3(cosf(i * 2.2f) * 4.2f, 0.4f, sinf(i * 2.2f) * 3.8f), vec3(1.4f, 1.0f, 1.2f), i * 1.1f, 0.2f, 0.4f + i * 0.2f, 0.2f, 0.6f);
      break;
    }
    case EK_SEASTACK: {
      int segs = lod == 0 ? 18 : lod == 1 ? 11 : 7, rings = lod == 0 ? 11 : lod == 1 ? 6 : 3;
      rockColumn(mb, vec3(0, -1.f, 0), segs, rings, 31.f, 9.6f, 7.2f, 0.71f, 0.045f, 0.18f, -0.3f);
      if (lod <= 1) rockColumn(mb, vec3(11.f, -1.f, 4.f), lod == 0 ? 10 : 7, lod == 0 ? 8 : 4, 16.f, 3.6f, 2.6f, 0.23f, 0.08f, 0.2f, 0.5f);
      if (lod == 0) for (int i = 0; i < 5; i++) rockBlob(mb, 0, vec3(cosf(i * 1.7f) * 10.5f, 0.3f, sinf(i * 1.7f) * 9.5f), vec3(2.3f, 1.5f, 2.f), i * 1.3f, 0.2f, 0.9f + i * 0.1f, 0.22f, 0.7f);
      break;
    }
  }
}

// ------------------------------------------------------------------ buildings (front faces +z, ground at y = 0)
void plinth(MB& mb, float hx, float hz, float top = 0.45f) { mb.box(vec3(-hx - 0.12f, -3.f, -hz - 0.12f), vec3(hx + 0.12f, top, hz + 0.12f), P_TRIM, 0x3B); }
void door(MB& mb, float x, float z, float w, float h, float y0 = 0.45f, bool frontZ = true) {
  if (frontZ) mb.box(vec3(x - w * 0.5f, y0, z - 0.02f), vec3(x + w * 0.5f, y0 + h, z + 0.06f), P_DOOR, 0x3B);
  else mb.box(vec3(z - 0.02f, y0, x - w * 0.5f), vec3(z + 0.06f, y0 + h, x + w * 0.5f), P_DOOR, 0x3B);
}

void buildBuilding(MB& mb, int kind, int lod) {
  bool d0 = lod == 0, d1 = lod <= 1;
  switch (kind) {
    case EK_HOUSE: {
      plinth(mb, 4.5f, 5.5f);
      mb.box(vec3(-4.5f, 0.45f, -5.5f), vec3(4.5f, 5.6f, 5.5f), P_WALL, 0x3B & ~8);
      mb.gable(-4.5f, 4.5f, -5.5f, 5.5f, 5.6f, 8.6f, true, d1 ? 0.5f : 0.f, P_ROOF, P_WALL);
      if (d1) mb.box(vec3(2.1f, 6.2f, -1.4f), vec3(2.9f, 9.4f, -0.6f), P_BRICK, 0x3B | 8);
      if (d0) { door(mb, -1.6f, 5.5f, 1.1f, 2.2f); mb.box(vec3(-2.7f, 2.85f, 5.5f), vec3(-0.5f, 3.0f, 6.7f), P_ROOF, 0x3F);
        mb.box(vec3(-2.6f, 0.45f, 6.5f), vec3(-2.45f, 2.85f, 6.65f), P_TRIM); mb.box(vec3(-0.75f, 0.45f, 6.5f), vec3(-0.6f, 2.85f, 6.65f), P_TRIM); }
      break;
    }
    case EK_HOUSE_HIP: {
      plinth(mb, 5.f, 5.f);
      mb.box(vec3(-5.f, 0.45f, -5.f), vec3(5.f, 3.4f, 5.f), P_WALL, 0x3B & ~8);
      mb.hip(-5.6f, 5.6f, -5.6f, 5.6f, 3.25f, 5.8f, P_ROOF);
      if (d1) mb.box(vec3(-3.2f, 4.f, 1.2f), vec3(-2.5f, 6.4f, 1.9f), P_BRICK, 0x3B | 8);
      if (d0) { door(mb, 1.5f, 5.f, 1.0f, 2.1f); mb.box(vec3(-5.f, 0.45f, 5.f), vec3(-1.5f, 0.6f, 6.6f), P_WOOD, 0x3F); }
      break;
    }
    case EK_HOUSE_L: {
      plinth(mb, 6.f, 6.f);
      mb.box(vec3(-6.f, 0.45f, -6.f), vec3(6.f, 5.2f, 0.5f), P_WALL, 0x3B & ~8);
      mb.gable(-6.f, 6.f, -6.f, 0.5f, 5.2f, 8.2f, false, d1 ? 0.45f : 0.f, P_ROOF, P_WALL);
      mb.box(vec3(-6.f, 0.45f, 0.5f), vec3(-0.6f, 5.2f, 6.f), P_WALL, 0x3B & ~8 & ~16);
      mb.gable(-6.f, -0.6f, 0.5f, 6.f, 5.2f, 7.9f, true, d1 ? 0.45f : 0.f, P_ROOF, P_WALL);
      if (d1) mb.box(vec3(3.2f, 5.6f, -4.2f), vec3(4.0f, 9.0f, -3.4f), P_BRICK, 0x3B | 8);
      if (d0) { door(mb, 1.6f, 0.5f, 1.1f, 2.2f); door(mb, 4.2f, 0.5f, 2.6f, 2.3f); }
      break;
    }
    case EK_FARMHOUSE: {
      plinth(mb, 5.5f, 4.5f);
      mb.box(vec3(-5.5f, 0.45f, -4.5f), vec3(5.5f, 6.2f, 4.5f), P_WALL, 0x3B & ~8);
      mb.gable(-5.5f, 5.5f, -4.5f, 4.5f, 6.2f, 9.f, false, d1 ? 0.5f : 0.f, P_ROOF, P_WALL);
      if (d1) { mb.box(vec3(-5.9f, 0.45f, -0.5f), vec3(-5.5f, 10.f, 0.5f), P_BRICK, 0x3B | 8); mb.box(vec3(5.5f, 0.45f, -0.5f), vec3(5.9f, 10.f, 0.5f), P_BRICK, 0x3B | 8); }
      if (d1) {   // front porch: deck, lean-to roof on posts
        mb.box(vec3(-5.f, 0.3f, 4.5f), vec3(5.f, 0.55f, 6.2f), P_WOOD, 0x3F);
        mb.quad(vec3(-5.2f, 3.3f, 4.5f), vec3(5.2f, 3.3f, 4.5f), vec3(5.2f, 2.85f, 6.4f), vec3(-5.2f, 2.85f, 6.4f), P_ROOF, vec3(0, 0, 4.5f));
        if (d0) for (int i = 0; i < 4; i++) { float x = -4.8f + i * 3.2f; mb.box(vec3(x - 0.09f, 0.55f, 5.9f), vec3(x + 0.09f, 2.9f, 6.08f), P_TRIM); }
      }
      if (d0) door(mb, 0.f, 4.5f, 1.1f, 2.3f);
      break;
    }
    case EK_TOWNHOUSE: {
      float hs[3] = {9.0f, 10.5f, 9.6f};
      plinth(mb, 9.f, 5.5f, 0.6f);
      for (int u = 0; u < 3; u++) {
        float x0 = -9.f + u * 6.f, x1 = x0 + 6.f, h = hs[u];
        mb.box(vec3(x0, 0.6f, -5.5f), vec3(x1, h, 5.5f), P_WALL, (u == 0 ? 1 : 0) | (u == 2 ? 2 : 0) | 8 | 16 | 32);
        if (u > 0 && hs[u - 1] != h) {   // party wall step
          float lo = std::min(hs[u - 1], h), hi = std::max(hs[u - 1], h);
          mb.quad(vec3(x0, lo, -5.5f), vec3(x0, hi, -5.5f), vec3(x0, hi, 5.5f), vec3(x0, lo, 5.5f), P_WALL, vec3(hs[u - 1] > h ? x0 + 1 : x0 - 1, lo, 0));
        }
        if (d1) mb.box(vec3(x0, h, 5.2f), vec3(x1, h + 0.6f, 5.75f), P_TRIM, 0x3F);   // cornice
        if (d0) { door(mb, x0 + 1.6f, 5.5f, 1.1f, 2.4f, 0.6f); mb.box(vec3(x0 + 0.9f, 0.f, 5.5f), vec3(x0 + 2.3f, 0.6f, 6.4f), P_TRIM, 0x3F); }
      }
      if (d0) mb.box(vec3(-3.f, 10.5f, -3.f), vec3(-1.2f, 11.6f, -1.6f), P_DARK, 0x3F);
      break;
    }
    case EK_SHOP: {
      plinth(mb, 7.f, 7.f, 0.3f);
      mb.box(vec3(-7.f, 0.3f, -7.f), vec3(7.f, 4.6f, 7.f), P_WALL, 0x3B);
      mb.box(vec3(-7.f, 4.6f, -7.f), vec3(7.f, 5.5f, 7.f), P_TRIM, 0x3B & ~8);   // parapet band
      mb.box(vec3(-6.4f, 4.4f, -6.4f), vec3(6.4f, 4.6f, 6.4f), P_DARK, 8);        // roof deck
      mb.box(vec3(-6.4f, 0.4f, 7.f), vec3(6.4f, 3.1f, 7.08f), P_GLASS, 32);       // storefront
      if (d1) mb.box(vec3(-6.6f, 3.6f, 7.f), vec3(6.6f, 4.45f, 7.12f), P_SIGN, 32);
      if (d0) {
        mb.quad(vec3(-6.8f, 3.45f, 7.f), vec3(6.8f, 3.45f, 7.f), vec3(6.8f, 2.85f, 8.7f), vec3(-6.8f, 2.85f, 8.7f), P_AWNING, vec3(0, 0, 7.f));
        door(mb, 0.f, 7.08f, 1.8f, 2.4f, 0.4f);
        mb.box(vec3(-3.5f, 4.6f, -3.f), vec3(-1.5f, 5.6f, -1.f), P_METAL, 0x3B | 8); mb.box(vec3(1.5f, 4.6f, -4.f), vec3(3.f, 5.3f, -2.6f), P_METAL, 0x3B | 8);
      }
      break;
    }
    case EK_APARTMENT: {
      plinth(mb, 9.f, 7.f, 0.5f);
      mb.box(vec3(-9.f, 0.5f, -7.f), vec3(9.f, 20.8f, 7.f), P_WALL, 0x3B);
      mb.box(vec3(-9.f, 20.8f, -7.f), vec3(9.f, 21.6f, 7.f), P_TRIM, 0x3B & ~8);
      mb.box(vec3(-8.6f, 20.6f, -6.6f), vec3(8.6f, 20.8f, 6.6f), P_DARK, 8);
      if (d1) mb.box(vec3(-2.5f, 20.8f, -2.f), vec3(2.5f, 23.2f, 2.f), P_TRIM, 0x3B | 8);
      if (d0) {
        for (int f = 1; f < 6; f++) {
          float y = 0.5f + f * 3.4f;
          for (int b = 0; b < 3; b++) {
            float x0 = -7.6f + b * 5.6f;
            mb.box(vec3(x0, y - 0.2f, 7.f), vec3(x0 + 3.6f, y, 8.3f), P_TRIM, 0x3F);
            mb.box(vec3(x0, y, 8.2f), vec3(x0 + 3.6f, y + 1.0f, 8.3f), P_GLASS, 0x3F);
          }
        }
        door(mb, 0.f, 7.f, 2.2f, 2.6f, 0.5f);
      }
      break;
    }
    case EK_OFFICE: {
      plinth(mb, 9.f, 9.f, 0.3f);
      mb.box(vec3(-8.6f, 0.3f, -8.6f), vec3(8.6f, 4.0f, 8.6f), P_GLASS, 0x33);       // recessed lobby glazing
      mb.box(vec3(-9.f, 3.9f, -9.f), vec3(9.f, 4.4f, 9.f), P_TRIM, 0x3F);
      mb.box(vec3(-9.f, 4.4f, -9.f), vec3(9.f, 36.f, 9.f), P_GLASS, 0x3B);
      mb.box(vec3(-9.3f, 36.f, -9.3f), vec3(9.3f, 37.4f, 9.3f), P_TRIM, 0x3F);
      if (d1) mb.box(vec3(-5.f, 37.4f, -4.f), vec3(5.f, 39.8f, 4.f), P_DARK, 0x3B | 8);
      if (d0) for (int i = 0; i < 4; i++) { float x = i & 1 ? 9.f : -9.f, z = i & 2 ? 9.f : -9.f; mb.box(vec3(x - 0.35f, 4.4f, z - 0.35f), vec3(x + 0.35f, 36.f, z + 0.35f), P_TRIM); }
      break;
    }
    case EK_TOWER: {
      plinth(mb, 10.f, 10.f, 0.3f);
      mb.box(vec3(-10.f, 0.3f, -10.f), vec3(10.f, 30.f, 10.f), P_WALL, 0x3B);
      mb.box(vec3(-10.3f, 30.f, -10.3f), vec3(10.3f, 31.f, 10.3f), P_TRIM, 0x3F);
      mb.box(vec3(-8.f, 31.f, -8.f), vec3(8.f, 56.f, 8.f), P_WALL, 0x3B);
      mb.box(vec3(-8.3f, 56.f, -8.3f), vec3(8.3f, 57.f, 8.3f), P_TRIM, 0x3F);
      mb.box(vec3(-6.f, 57.f, -6.f), vec3(6.f, 74.f, 6.f), P_WALL, 0x3B);
      mb.box(vec3(-6.4f, 74.f, -6.4f), vec3(6.4f, 75.2f, 6.4f), P_TRIM, 0x3F);
      if (d1) mb.cyl(vec3(0, 75.2f, 0), vec3(0, 80.f, 0), 0.5f, 0.12f, 6, P_METAL, false, false);
      break;
    }
    case EK_SKYSCRAPER: {
      plinth(mb, 10.f, 10.f, 0.3f);
      mb.box(vec3(-10.5f, 0.3f, -10.5f), vec3(10.5f, 7.f, 10.5f), P_TRIM, 0x3B);
      int sg = 8;
      mb.cyl(vec3(0, 7.f, 0), vec3(0, 118.f, 0), 9.6f, 9.6f, sg, P_GLASS, false, false, 1, 1, PI / 8, false);
      mb.cyl(vec3(0, 118.f, 0), vec3(0, 127.f, 0), 9.6f, 6.6f, sg, P_GLASS, true, false, 1, 1, PI / 8, false);
      if (d1) mb.cyl(vec3(0, 127.f, 0), vec3(0, 135.f, 0), 1.0f, 0.05f, 6, P_METAL, false, false);
      if (d0) for (int i = 0; i < sg; i++) { float a = PI / 8 + i * 2 * PI / sg; vec3 c(cosf(a) * 9.7f, 0, sinf(a) * 9.7f); mb.box(vec3(c.x - 0.3f, 7.f, c.z - 0.3f), vec3(c.x + 0.3f, 118.f, c.z + 0.3f), P_TRIM); }
      break;
    }
    case EK_WAREHOUSE: {
      plinth(mb, 12.f, 9.f, 0.6f);
      mb.box(vec3(-12.f, 0.6f, -9.f), vec3(12.f, 6.6f, 9.f), P_WALL, 0x3B & ~8);
      mb.gable(-12.f, 12.f, -9.f, 9.f, 6.6f, 8.5f, false, d1 ? 0.4f : 0.f, P_ROOF, P_WALL);
      if (d1) { door(mb, -5.f, 9.f, 4.f, 4.6f, 0.6f); door(mb, 3.f, 9.f, 4.f, 4.6f, 0.6f); }
      if (d0) { mb.box(vec3(-9.f, 0.f, 9.f), vec3(9.f, 1.1f, 10.6f), P_TRIM, 0x3F); door(mb, 9.f, 9.f, 1.0f, 2.2f, 1.1f); }
      break;
    }
    case EK_BARN: {
      plinth(mb, 6.f, 9.f, 0.3f);
      mb.box(vec3(-6.f, 0.3f, -9.f), vec3(6.f, 4.5f, 9.f), P_WOOD, 0x3B & ~8);
      // gambrel roof: profile extruded along z
      float ov = d1 ? 0.35f : 0.f;
      vec2 prof[5] = {vec2(-6.4f, 4.25f), vec2(-4.6f, 7.6f), vec2(0.f, 10.f), vec2(4.6f, 7.6f), vec2(6.4f, 4.25f)};
      vec3 ref(0, 5.f, 0);
      for (int i = 0; i < 4; i++)
        mb.quad(vec3(prof[i].x, prof[i].y, -9.f - ov), vec3(prof[i + 1].x, prof[i + 1].y, -9.f - ov), vec3(prof[i + 1].x, prof[i + 1].y, 9.f + ov), vec3(prof[i].x, prof[i].y, 9.f + ov), P_ROOF, ref);
      for (int s = 0; s < 2; s++) {   // gable ends in planks
        float z = s ? 9.f : -9.f;
        vec3 c(0, 6.f, z);
        vec2 w[5] = {vec2(-6.f, 4.5f), vec2(-4.4f, 7.5f), vec2(0.f, 9.8f), vec2(4.4f, 7.5f), vec2(6.f, 4.5f)};
        for (int i = 0; i < 4; i++) mb.tri(vec3(w[i].x, w[i].y, z), vec3(w[i + 1].x, w[i + 1].y, z), c, P_WOOD, vec3(0, 6.f, 0));
        mb.tri(vec3(-6.f, 4.5f, z), vec3(6.f, 4.5f, z), c, P_WOOD, vec3(0, 6.f, 0));
      }
      if (d1) { door(mb, 0.f, 9.f, 4.4f, 4.0f, 0.3f); door(mb, 0.f, 9.f, 1.8f, 1.6f, 6.0f); }
      if (d0) { mb.box(vec3(-2.2f, 0.3f, 9.06f), vec3(2.2f, 0.5f, 9.14f), P_TRIM); mb.box(vec3(-2.2f, 4.1f, 9.06f), vec3(2.2f, 4.3f, 9.14f), P_TRIM); }
      break;
    }
    case EK_SILO: {
      int sg = lod == 0 ? 14 : lod == 1 ? 9 : 6;
      mb.box(vec3(-3.3f, -2.f, -3.3f), vec3(3.3f, 0.4f, 3.3f), P_TRIM, 0x3B);
      mb.cyl(vec3(0, 0.4f, 0), vec3(0, 16.f, 0), 3.f, 3.f, sg, P_METAL, false, false);
      blob(mb, lod == 0 ? 1 : 0, [&](vec3 d) { return vec3(d.x * 3.05f, 16.f + std::max(d.y, 0.f) * 2.6f, d.z * 3.05f); }, [](vec3, vec3) { return 1.f; }, P_METAL);
      if (d0) mb.box(vec3(2.95f, 0.4f, -0.3f), vec3(3.2f, 17.f, 0.3f), P_DARK, 0x3F);
      break;
    }
    case EK_CHURCH: {
      plinth(mb, 5.f, 13.f, 0.5f);
      mb.box(vec3(-5.f, 0.5f, -13.f), vec3(5.f, 7.f, 5.f), P_WALL, 0x3B & ~8);
      mb.gable(-5.f, 5.f, -13.f, 5.f, 7.f, 13.f, true, d1 ? 0.4f : 0.f, P_ROOF, P_WALL);
      mb.box(vec3(-2.6f, 0.5f, 5.f), vec3(2.6f, 18.f, 10.2f), P_WALL, 0x3B);
      if (d1) mb.box(vec3(-2.8f, 17.6f, 4.8f), vec3(2.8f, 18.3f, 10.4f), P_TRIM, 0x3F);
      // spire: four-sided pyramid
      { vec3 a(-2.6f, 18.3f, 5.f), b(2.6f, 18.3f, 5.f), c(2.6f, 18.3f, 10.2f), d(-2.6f, 18.3f, 10.2f), t(0, 27.4f, 7.6f), ref(0, 19.f, 7.6f);
        mb.tri(a, b, t, P_ROOF, ref); mb.tri(b, c, t, P_ROOF, ref); mb.tri(c, d, t, P_ROOF, ref); mb.tri(d, a, t, P_ROOF, ref); }
      if (d0) { mb.box(vec3(-0.08f, 27.2f, 7.52f), vec3(0.08f, 28.4f, 7.68f), P_METAL); mb.box(vec3(-0.45f, 27.8f, 7.52f), vec3(0.45f, 27.95f, 7.68f), P_METAL);
        door(mb, 0.f, 10.2f, 1.8f, 3.2f, 0.5f); }
      break;
    }
    case EK_WATERTOWER: {
      int sg = lod == 0 ? 14 : lod == 1 ? 9 : 6;
      for (int i = 0; i < 4; i++) { float a = PI * 0.25f + i * PI * 0.5f; vec3 f(cosf(a) * 3.6f, -1.f, sinf(a) * 3.6f), t(cosf(a) * 2.6f, 18.2f, sinf(a) * 2.6f);
        mb.cyl(f, t, 0.28f, 0.22f, lod == 0 ? 5 : 3, P_METAL, false, false); }
      if (d0) for (int i = 0; i < 4; i++) { float a0 = PI * 0.25f + i * PI * 0.5f, a1 = a0 + PI * 0.5f;
        for (int h = 0; h < 2; h++) { float y0 = 2.f + h * 8.f, y1 = y0 + 7.f, r0 = lerpf(3.6f, 2.6f, y0 / 19.f), r1 = lerpf(3.6f, 2.6f, y1 / 19.f);
          mb.cyl(vec3(cosf(a0) * r0, y0, sinf(a0) * r0), vec3(cosf(a1) * r1, y1, sinf(a1) * r1), 0.08f, 0.08f, 3, P_METAL, false, false); } }
      mb.cyl(vec3(0, 18.f, 0), vec3(0, 25.f, 0), 5.2f, 5.2f, sg, P_METAL, false, true);
      mb.cyl(vec3(0, 25.f, 0), vec3(0, 28.f, 0), 5.3f, 0.f, sg, P_METAL, false, false);
      if (d0) mb.cyl(vec3(0, 18.f, 0), vec3(0, 18.2f, 0), 6.f, 6.f, sg, P_DARK, true, true);
      break;
    }
    case EK_LIGHTHOUSE: {
      int sg = lod == 0 ? 16 : lod == 1 ? 10 : 6;
      mb.box(vec3(-3.4f, -2.f, -3.4f), vec3(3.4f, 1.5f, 3.4f), P_TRIM, 0x3B);
      mb.cyl(vec3(0, 1.5f, 0), vec3(0, 22.f, 0), 3.0f, 2.1f, sg, P_WALL, false, false);
      mb.cyl(vec3(0, 22.f, 0), vec3(0, 22.45f, 0), 2.9f, 2.9f, sg, P_DARK, true, true);
      mb.cyl(vec3(0, 22.45f, 0), vec3(0, 25.f, 0), 1.6f, 1.6f, sg, P_LAMP, false, false);
      blob(mb, lod == 0 ? 1 : 0, [&](vec3 d) { return vec3(d.x * 1.75f, 25.f + std::max(d.y, 0.f) * 2.f, d.z * 1.75f); }, [](vec3, vec3) { return 1.f; }, P_METAL);
      if (d0) { mb.box(vec3(-2.6f, 1.5f, 2.6f), vec3(-1.4f, 3.6f, 2.95f), P_DOOR); mb.box(vec3(-6.f, -1.f, -4.f), vec3(-3.4f, 4.f, 4.f), P_WALL, 0x3B & ~8);
        mb.gable(-6.f, -3.4f, -4.f, 4.f, 4.f, 5.4f, true, 0.3f, P_ROOF, P_WALL); }
      break;
    }
    case EK_GASSTATION: {
      mb.box(vec3(-8.f, -1.f, -7.f), vec3(8.f, 0.15f, 7.f), P_TRIM, 0x3F);                 // forecourt slab
      mb.box(vec3(-4.5f, 0.15f, -7.f), vec3(4.5f, 3.6f, -3.2f), P_WALL, 0x3B);           // kiosk
      mb.box(vec3(-4.f, 0.4f, -3.2f), vec3(4.f, 2.8f, -3.12f), P_GLASS, 32);
      mb.box(vec3(-7.6f, 4.6f, -1.6f), vec3(7.6f, 5.5f, 6.6f), P_CANOPY, 0x3F);           // canopy
      if (d1) for (int i = 0; i < 4; i++) { float x = i & 1 ? 5.5f : -5.5f, z = i & 2 ? 4.6f : 0.4f; mb.box(vec3(x - 0.25f, 0.15f, z - 0.25f), vec3(x + 0.25f, 4.6f, z + 0.25f), P_TRIM); }
      if (d0) {
        for (int i = 0; i < 3; i++) { float x = -4.f + i * 4.f; mb.box(vec3(x - 0.4f, 0.15f, 2.2f), vec3(x + 0.4f, 1.7f, 2.8f), P_METAL, 0x3B | 8); }
        mb.box(vec3(6.6f, 0.15f, 6.4f), vec3(7.0f, 5.f, 6.8f), P_TRIM); mb.box(vec3(5.8f, 5.f, 6.45f), vec3(7.8f, 7.f, 6.75f), P_SIGN, 0x3F);
        door(mb, 2.5f, -3.12f, 1.6f, 2.3f, 0.15f);
      }
      break;
    }
  }
}
}  // namespace

void buildEntityMeshes(std::vector<EVert>& out, EntMeshRange ranges[EK_COUNT]) {
  out.clear();
  MB mb(out);
  for (int k = 0; k < EK_COUNT; k++)
    for (int l = 0; l < ENT_LODS; l++) {
      ranges[k].first[l] = (int)out.size();
      int cls = entClass(k);
      if (cls == EC_TREE) buildTree(mb, k, l);
      else if (cls == EC_ROCK) buildRock(mb, k, l);
      else buildBuilding(mb, k, l);
      ranges[k].count[l] = (int)out.size() - ranges[k].first[l];
    }
}
