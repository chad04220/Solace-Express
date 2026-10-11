// Solace Express - the bridges' meshes (bridge_mesh.h)
#include "bridge_mesh.h"
#include <algorithm>
#include <cmath>

namespace {

struct Mesh {
  std::vector<EVert>& v;
  void tri(vec3 a, vec3 b, vec3 c, vec3 na, vec3 nb, vec3 nc, float part, float ao, vec2 ua = vec2(), vec2 ub = vec2(), vec2 uc = vec2()) {
    v.push_back({a.x, a.y, a.z, na.x, na.y, na.z, part, ao, ua.x, ua.y});
    v.push_back({b.x, b.y, b.z, nb.x, nb.y, nb.z, part, ao, ub.x, ub.y});
    v.push_back({c.x, c.y, c.z, nc.x, nc.y, nc.z, part, ao, uc.x, uc.y});
  }
  // a quad a b c d (in order round it), each corner with its own normal
  void quad(vec3 a, vec3 b, vec3 c, vec3 d, vec3 na, vec3 nb, vec3 nc, vec3 nd, float part, float ao,
            vec2 ua = vec2(), vec2 ub = vec2(), vec2 uc = vec2(), vec2 ud = vec2()) {
    tri(a, b, c, na, nb, nc, part, ao, ua, ub, uc);
    tri(a, c, d, na, nc, nd, part, ao, ua, uc, ud);
  }
  void flat(vec3 a, vec3 b, vec3 c, vec3 d, vec3 n, float part, float ao) { quad(a, b, c, d, n, n, n, n, part, ao); }
  // an upright box in the frame (x across, z along) about c, from y0 to y1, its vertical edges cut back by chamfer
  void prism(vec3 c, vec3 x, vec3 z, float hx, float hz, float y0, float y1, float chamfer, float part, float ao, bool top) {
    std::vector<vec2> poly;   // counter-clockwise seen from above (x, z)
    if (chamfer <= 0.f) poly = {vec2(-hx, -hz), vec2(hx, -hz), vec2(hx, hz), vec2(-hx, hz)};
    else poly = {vec2(-hx + chamfer, -hz), vec2(hx - chamfer, -hz), vec2(hx, -hz + chamfer), vec2(hx, hz - chamfer),
                 vec2(hx - chamfer, hz), vec2(-hx + chamfer, hz), vec2(-hx, hz - chamfer), vec2(-hx, -hz + chamfer)};
    auto at = [&](vec2 p, float y) { return vec3(c.x, y, c.z) + x * p.x + z * p.y; };
    const size_t n = poly.size();
    for (size_t i = 0; i < n; i++) {
      const vec2 p0 = poly[i], p1 = poly[(i + 1) % n], d = p1 - p0;
      const vec3 nw = normalize(x * d.y - z * d.x);   // (outward: the polygon runs counter-clockwise)
      flat(at(p0, y0), at(p1, y0), at(p1, y1), at(p0, y1), nw, part, ao);
    }
    if (top) for (size_t i = 1; i + 1 < n; i++) tri(at(poly[0], y1), at(poly[i], y1), at(poly[i + 1], y1), vec3(0, 1, 0), vec3(0, 1, 0), vec3(0, 1, 0), part, ao);
    for (size_t i = 1; i + 1 < n; i++) tri(at(poly[0], y0), at(poly[i + 1], y0), at(poly[i], y0), vec3(0, -1, 0), vec3(0, -1, 0), vec3(0, -1, 0), part, ao);
  }
};

struct ProfileEdge { vec2 a, b; float part, ao; };

}  // namespace

void buildBridgeMeshes(const std::vector<Bridge>& bridges, std::vector<EVert>& out, std::vector<BridgeMeshRange>& ranges) {
  ranges.clear();
  Mesh m{out};
  const vec3 up(0, 1, 0);
  for (const Bridge& b : bridges) {
    const int first = (int)out.size();
    const size_t n = b.deck.size();
    if (n < 2) { ranges.push_back({first, 0}); continue; }
    // per deck point: its place on the centreline and the level direction across the deck (mitred between the segments
    // either side, so the faces of neighbouring segments meet)
    std::vector<vec3> c(n), ac(n), dir(n);
    for (size_t i = 0; i < n; i++) c[i] = vec3(b.deck[i].x, b.deck[i].h, b.deck[i].z);
    for (size_t i = 0; i < n; i++) {
      const vec3 d0 = i > 0 ? normalize(vec3(c[i].x - c[i - 1].x, 0, c[i].z - c[i - 1].z)) : vec3(),
                 d1 = i + 1 < n ? normalize(vec3(c[i + 1].x - c[i].x, 0, c[i + 1].z - c[i].z)) : vec3();
      dir[i] = normalize(d0 + d1);
      vec3 a = vec3(-dir[i].z, 0, dir[i].x);
      const vec3 a0 = i > 0 ? vec3(-d0.z, 0, d0.x) : a;
      const float cs = std::max(dot(a, a0), 0.5f);   // (the mitre: the deck keeps its width through a bend)
      ac[i] = a / cs;
    }
    // its ends square to the road running onto it, where the road's graded bed stops square too
    if (b.inX != 0.f || b.inZ != 0.f) { dir[0] = vec3(b.inX, 0, b.inZ); ac[0] = vec3(-b.inZ, 0, b.inX); }
    if (b.outX != 0.f || b.outZ != 0.f) { dir[n - 1] = vec3(b.outX, 0, b.outZ); ac[n - 1] = vec3(-b.outZ, 0, b.outX); }
    const float H = b.halfDeck, w = 0.35f, s = 0.35f, D = b.depth, g = H * 0.72f;
    const bool barrier = b.cls <= RC_ROAD || b.stone;
    const float par = b.stone ? 1.0f : barrier ? bridgeParapet(b.cls) : 0.25f;   // (the lanes: a kerb, the steel rail above it)
    const float wall = b.stone ? 11.f : 5.f;   // (P_BRICK: a masonry viaduct's stone; else concrete)
    // the structure's cross-section, counter-clockwise (across, up from the road surface): the parapets or kerbs, the
    // slab's edges and its overhangs' undersides, the box girder - a masonry viaduct's a solid slab down to its arches'
    // crowns, its spandrels below (with the arches, further on)
    std::vector<ProfileEdge> prof = {
      {vec2(-(H - w), 0), vec2(-(H - w), par), wall, 1.f}, {vec2(-(H - w), par), vec2(-H, par), wall, 1.f}};
    if (b.stone) prof.insert(prof.end(), {{vec2(-H, par), vec2(-H, -D), wall, 1.f}, {vec2(-H, -D), vec2(H, -D), wall, 0.75f}, {vec2(H, -D), vec2(H, par), wall, 1.f}});
    else prof.insert(prof.end(), {{vec2(-H, par), vec2(-H, -s), 5, 1.f}, {vec2(-H, -s), vec2(-g, -s), 5, 0.72f}, {vec2(-g, -s), vec2(-g, -D), 5, 0.8f},
                                  {vec2(-g, -D), vec2(g, -D), 5, 0.78f}, {vec2(g, -D), vec2(g, -s), 5, 0.8f}, {vec2(g, -s), vec2(H, -s), 5, 0.72f},
                                  {vec2(H, -s), vec2(H, par), 5, 1.f}});
    prof.insert(prof.end(), {{vec2(H, par), vec2(H - w, par), wall, 1.f}, {vec2(H - w, par), vec2(H - w, 0), wall, 1.f}});
    auto at = [&](size_t i, vec2 p) { return c[i] + ac[i] * p.x + up * p.y; };
    for (size_t i = 0; i + 1 < n; i++) {
      // the road surface, between the parapets: across and along for its markings (the road's own distance along)
      const float r = H - w;
      m.quad(at(i, vec2(-r, 0)), at(i, vec2(r, 0)), at(i + 1, vec2(r, 0)), at(i + 1, vec2(-r, 0)), up, up, up, up, (float)(P_DECK + b.cls), 1.f,
             vec2(-r, b.along[i]), vec2(r, b.along[i]), vec2(r, b.along[i + 1]), vec2(-r, b.along[i + 1]));
      for (const ProfileEdge& e : prof) {
        const vec2 d = e.b - e.a, nn = vec2(d.y, -d.x) * (1.f / std::max(length(d), 1e-6f));
        const vec3 n0 = normalize(ac[i] * nn.x + up * nn.y), n1 = normalize(ac[i + 1] * nn.x + up * nn.y);
        m.quad(at(i, e.a), at(i, e.b), at(i + 1, e.b), at(i + 1, e.a), n0, n0, n1, n1, e.part, e.ao);
      }
    }
    // the ends: the cross-section closed (the parapets or kerbs, the slab, the girder)
    for (int end = 0; end < 2; end++) {
      const size_t i = end ? n - 1 : 0;
      const vec3 nn = dir[i] * (end ? 1.f : -1.f);
      auto rect = [&](float x0, float x1, float y0, float y1) { m.flat(at(i, vec2(x0, y0)), at(i, vec2(x1, y0)), at(i, vec2(x1, y1)), at(i, vec2(x0, y1)), nn, wall, 0.9f); };
      rect(-H, -(H - w), 0, par); rect(H - w, H, 0, par);
      if (b.stone) rect(-H, H, -D, 0);
      else { rect(-H, H, -s, 0); rect(-g, g, -D, -s); }
    }
    // the lanes' steel rail: posts every 2.5 m along the kerbs and two rails along their tops
    if (!barrier) {
      const float rail = bridgeParapet(b.cls);
      for (float side : {-1.f, 1.f}) {
        const float x = side * (H - w * 0.5f);
        for (size_t i = 0; i + 1 < n; i++) {
          const float len = length(c[i + 1] - c[i]);
          const int posts = std::max(1, (int)(len / 2.5f));
          for (int k = 0; k < posts; k++) {
            const float t = (float)k / posts;
            const vec3 p = at(i, vec2(x, par)) * (1.f - t) + at(i + 1, vec2(x, par)) * t;
            m.prism(p, ac[i] / length(ac[i]), dir[i], 0.06f, 0.06f, p.y, p.y + rail - par, 0.f, 9, 1.f, false);
          }
          for (float y : {rail - 0.05f, rail * 0.55f}) {   // (the rails: a box section along the deck)
            const vec2 q0(x - 0.05f, y - 0.05f), q1(x + 0.05f, y - 0.05f), q2(x + 0.05f, y + 0.05f), q3(x - 0.05f, y + 0.05f);
            const vec2 sq[5] = {q0, q1, q2, q3, q0};
            for (int e = 0; e < 4; e++) {
              const vec2 d = sq[e + 1] - sq[e], nn = vec2(d.y, -d.x) * (1.f / length(d));
              const vec3 n0 = normalize(ac[i] * nn.x + up * nn.y), n1 = normalize(ac[i + 1] * nn.x + up * nn.y);
              m.quad(at(i, sq[e]), at(i, sq[e + 1]), at(i + 1, sq[e + 1]), at(i + 1, sq[e]), n0, n0, n1, n1, 9, 1.f);
            }
          }
        }
      }
    }
    // the abutments: a wall under each end, from the girders down into the ground (where the ground is below them)
    for (int end = 0; end < 2; end++) {
      const size_t i = end ? n - 1 : 0;
      const vec3 away = dir[i] * (end ? 1.f : -1.f);
      const RoadPoint& p = b.deck[i];
      const float top = p.h - D, foot = b.water ? -3.f : top - 12.f;   // (down into the bank: the ground hides the rest)
      m.prism(vec3(c[i].x, 0.f, c[i].z) + away * 0.6f, ac[i] / length(ac[i]), away, H, 1.4f, foot, top, 0.f, wall, 0.85f, true);
    }
    // the piers: a chamfered shaft and a cap under the girders
    for (const BridgePier& pr : b.piers) {
      const vec3 along(pr.ux, 0, pr.uz), across(-pr.uz, 0, pr.ux), ctr(pr.x, 0, pr.z);
      if (b.stone) { m.prism(ctr, across, along, pr.halfWidth, pr.halfThick, pr.bottom, pr.top, 0.f, wall, 0.86f, false); continue; }
      const float cap = b.cls <= RC_ROAD ? 1.2f : 0.8f;
      const float ch = std::min(pr.halfWidth, pr.halfThick) * 0.45f;
      m.prism(ctr, across, along, pr.halfWidth, pr.halfThick, pr.bottom, pr.top - cap, ch, 5, 0.88f, false);
      m.prism(ctr, across, along, std::min(pr.halfWidth + 0.45f, g), pr.halfThick + 0.3f, pr.top - cap, pr.top, 0.f, 5, 0.82f, true);
    }
    // a masonry viaduct's arches: the spandrel walls down each side from the slab to the arch, the arch's soffit under
    // them (u 1: the arch ring's, v its height over the intrados - the voussoirs along it, roads.glsl's bridge stone)
    for (const BridgeArch& a : b.arches) {
      const int steps = std::max(8, (int)ceilf((a.s1 - a.s0) / 1.5f));
      for (int k = 0; k < steps; k++) {
        const float sa = a.s0 + (a.s1 - a.s0) * k / steps, sb = a.s0 + (a.s1 - a.s0) * (k + 1) / steps;
        vec3 ca, da, xa, cb, db, xb;
        bridgeFrame(b, sa, ca, da, xa); bridgeFrame(b, sb, cb, db, xb);
        const float ia = bridgeIntrados(b, sa + (k == 0 ? 1e-3f : 0.f)), ib = bridgeIntrados(b, sb - (k + 1 == steps ? 1e-3f : 0.f));
        const float ta = ca.y - D, tb = cb.y - D;
        for (float side : {-1.f, 1.f}) {
          const vec3 pa = ca + xa * (side * H), pb = cb + xb * (side * H), nw = normalize(xa + xb) * side;
          const vec3 q0(pa.x, ia, pa.z), q1(pb.x, ib, pb.z), q2(pb.x, tb, pb.z), q3(pa.x, ta, pa.z);
          if (side > 0) m.quad(q0, q1, q2, q3, nw, nw, nw, nw, 11.f, 0.85f, vec2(1, 0), vec2(1, 0), vec2(1, tb - ib), vec2(1, ta - ia));
          else m.quad(q1, q0, q3, q2, nw, nw, nw, nw, 11.f, 0.85f, vec2(1, 0), vec2(1, 0), vec2(1, ta - ia), vec2(1, tb - ib));
        }
        const vec3 l0 = vec3(ca.x, ia, ca.z) - xa * H, r0 = vec3(ca.x, ia, ca.z) + xa * H, l1 = vec3(cb.x, ib, cb.z) - xb * H, r1 = vec3(cb.x, ib, cb.z) + xb * H;
        vec3 nd = normalize(cross(r0 - l0, l1 - l0)); if (nd.y > 0.f) nd = nd * -1.f;   // (the soffit faces down)
        m.quad(l0, l1, r1, r0, nd, nd, nd, nd, 11.f, 0.7f, vec2(1, 0), vec2(1, 0), vec2(1, 0), vec2(1, 0));
      }
    }
    // a main span's steel: each member a box section (P_PAINT, u its colour: 0 a truss's grey-green, 1 an arch's white)
    forEachBridgeMember(b, [&](const BridgeMember& mb) {
      const vec3 u = mb.b - mb.a; const float l = length(u);
      if (l < 1e-3f) return;
      const vec3 z = u / l, x0 = cross(vec3(0.f, 1.f, 0.f), z);
      const vec3 x = length(x0) > 1e-3f ? normalize(x0) : vec3(1.f, 0.f, 0.f), y = cross(z, x);
      const vec2 cu((float)mb.colour, 0.f);
      const vec3 corner[4] = {x * -mb.hw + y * -mb.hh, x * mb.hw + y * -mb.hh, x * mb.hw + y * mb.hh, x * -mb.hw + y * mb.hh};
      const vec3 face[4] = {y * -1.f, x, y, x * -1.f};
      for (int f = 0; f < 4; f++) {
        const vec3 p0 = corner[f], p1 = corner[(f + 1) % 4];
        m.quad(mb.a + p0, mb.a + p1, mb.b + p1, mb.b + p0, face[f], face[f], face[f], face[f], 21.f, 0.95f, cu, cu, cu, cu);
      }
      for (int end = 0; end < 2; end++) {   // (its ends capped)
        const vec3 o = end ? mb.b : mb.a, nz = end ? z : z * -1.f;
        if (end) m.quad(o + corner[0], o + corner[1], o + corner[2], o + corner[3], nz, nz, nz, nz, 21.f, 0.9f, cu, cu, cu, cu);
        else m.quad(o + corner[3], o + corner[2], o + corner[1], o + corner[0], nz, nz, nz, nz, 21.f, 0.9f, cu, cu, cu, cu);
      }
    });
    ranges.push_back({first, (int)out.size() - first});
  }
}
