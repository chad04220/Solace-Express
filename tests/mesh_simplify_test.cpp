// The aircraft mesh simplifier (src/mesh_simplify.h): a finely tessellated box and sphere, as the surface nets bake
// would leave them, must lose most of their triangles where the surface is flat, keep every original vertex within the
// error bound of the simplified surface, never turn a face inward, and never move a material boundary.
#include "mesh_simplify.h"
#include <cstdio>
#include <cmath>
#include <vector>
#include <map>

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

// (in double precision: the simplified faces are long and thin, and float loses the distance inside them)
struct D3 { double x, y, z; };
static D3 sub(D3 a, D3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
static double dt(D3 a, D3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static D3 mad(D3 a, D3 b, double s) { return {a.x + b.x * s, a.y + b.y * s, a.z + b.z * s}; }
static vec3 closestOnTri(const vec3& pf, const vec3& af, const vec3& bf, const vec3& cf) {
  D3 p{pf.x, pf.y, pf.z}, a{af.x, af.y, af.z}, b{bf.x, bf.y, bf.z}, c{cf.x, cf.y, cf.z};
  auto out = [](D3 q) { return vec3((float)q.x, (float)q.y, (float)q.z); };
  D3 ab = sub(b, a), ac = sub(c, a), ap = sub(p, a);
  double d1 = dt(ab, ap), d2 = dt(ac, ap);
  if (d1 <= 0 && d2 <= 0) return af;
  D3 bp = sub(p, b); double d3 = dt(ab, bp), d4 = dt(ac, bp);
  if (d3 >= 0 && d4 <= d3) return bf;
  double vc = d1 * d4 - d3 * d2;
  if (vc <= 0 && d1 >= 0 && d3 <= 0) return out(mad(a, ab, d1 / (d1 - d3)));
  D3 cp = sub(p, c); double d5 = dt(ab, cp), d6 = dt(ac, cp);
  if (d6 >= 0 && d5 <= d6) return cf;
  double vb = d5 * d2 - d1 * d6;
  if (vb <= 0 && d2 >= 0 && d6 <= 0) return out(mad(a, ac, d2 / (d2 - d6)));
  double va = d3 * d6 - d5 * d4;
  if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) return out(mad(b, sub(c, b), (d4 - d3) / ((d4 - d3) + (d5 - d6))));
  double den = 1.0 / (va + vb + vc);
  return out(mad(mad(a, ab, vb * den), ac, vc * den));
}

struct Mesh { std::vector<float> vb; std::vector<uint32_t> ib; };

// the worst distance from the original vertices to the simplified surface
static float deviation(const Mesh& orig, const Mesh& simp) {
  float worst = 0;
  for (size_t v = 0; v < orig.vb.size() / 8; v++) {
    vec3 p(orig.vb[v * 8], orig.vb[v * 8 + 1], orig.vb[v * 8 + 2]);
    float best = 1e9f;
    for (size_t t = 0; t < simp.ib.size() / 3; t++) {
      auto P = [&](uint32_t i) { return vec3(simp.vb[i * 8], simp.vb[i * 8 + 1], simp.vb[i * 8 + 2]); };
      vec3 q = closestOnTri(p, P(simp.ib[t * 3]), P(simp.ib[t * 3 + 1]), P(simp.ib[t * 3 + 2]));
      best = std::min(best, length(q - p));
    }
    worst = std::max(worst, best);
  }
  return worst;
}
// faces that turn towards the centre (the shapes are convex about the origin)
static int inward(const Mesh& m) {
  int n = 0;
  for (size_t t = 0; t < m.ib.size() / 3; t++) {
    auto P = [&](uint32_t i) { return vec3(m.vb[i * 8], m.vb[i * 8 + 1], m.vb[i * 8 + 2]); };
    vec3 a = P(m.ib[t * 3]), b = P(m.ib[t * 3 + 1]), c = P(m.ib[t * 3 + 2]);
    if (dot(cross(b - a, c - a), (a + b + c) * (1.f / 3.f)) <= 0) n++;
  }
  return n;
}

// a box of half size h, each face an n x n grid, the vertices shared along the edges; the top face's middle third is
// another material (id 2), as a painted panel would be
static Mesh box(float h, int n) {
  Mesh m; std::map<std::tuple<int, int, int>, uint32_t> at;
  auto vert = [&](int i, int j, int k) {
    auto key = std::make_tuple(i, j, k);
    auto it = at.find(key); if (it != at.end()) return it->second;
    vec3 p(-h + 2 * h * i / n, -h + 2 * h * j / n, -h + 2 * h * k / n);
    vec3 nn((i == 0 ? -1.f : i == n ? 1.f : 0.f), (j == 0 ? -1.f : j == n ? 1.f : 0.f), (k == 0 ? -1.f : k == n ? 1.f : 0.f));
    nn = normalize(nn);
    float id = (j == n && i > n / 3 && i < 2 * n / 3 && k > n / 3 && k < 2 * n / 3) ? 2.f : 1.f;
    uint32_t idx = (uint32_t)(m.vb.size() / 8);
    m.vb.insert(m.vb.end(), {p.x, p.y, p.z, nn.x, nn.y, nn.z, id, 1.f});
    at[key] = idx; return idx;
  };
  auto quad = [&](uint32_t a, uint32_t b, uint32_t c, uint32_t d) { m.ib.insert(m.ib.end(), {a, b, c, a, c, d}); };
  for (int u = 0; u < n; u++) for (int w = 0; w < n; w++) {
    quad(vert(0, u, w), vert(0, u, w + 1), vert(0, u + 1, w + 1), vert(0, u + 1, w));
    quad(vert(n, u, w), vert(n, u + 1, w), vert(n, u + 1, w + 1), vert(n, u, w + 1));
    quad(vert(u, 0, w), vert(u + 1, 0, w), vert(u + 1, 0, w + 1), vert(u, 0, w + 1));
    quad(vert(u, n, w), vert(u, n, w + 1), vert(u + 1, n, w + 1), vert(u + 1, n, w));
    quad(vert(u, w, 0), vert(u, w + 1, 0), vert(u + 1, w + 1, 0), vert(u + 1, w, 0));
    quad(vert(u, w, n), vert(u + 1, w, n), vert(u + 1, w + 1, n), vert(u, w + 1, n));
  }
  return m;
}
static Mesh sphere(float r, int nu, int nw) {
  Mesh m;
  auto add = [&](vec3 n) { m.vb.insert(m.vb.end(), {n.x * r, n.y * r, n.z * r, n.x, n.y, n.z, 1.f, 1.f}); };
  add(vec3(0, 1, 0));   // the poles: one vertex each, the rows of nw between them
  for (int j = 1; j < nu; j++) for (int i = 0; i < nw; i++) {
    float th = 3.14159265f * j / nu, ph = 6.2831853f * i / nw;
    add(vec3(sinf(th) * cosf(ph), cosf(th), sinf(th) * sinf(ph)));
  }
  add(vec3(0, -1, 0));
  auto V = [&](int j, int i) { if (j == 0) return 0u; if (j == nu) return (uint32_t)(1 + (nu - 1) * nw); return (uint32_t)(1 + (j - 1) * nw + (i % nw)); };
  for (int j = 0; j < nu; j++) for (int i = 0; i < nw; i++) {
    uint32_t a = V(j, i), b = V(j, i + 1), c = V(j + 1, i + 1), d = V(j + 1, i);
    if (j > 0) m.ib.insert(m.ib.end(), {a, b, c});
    if (j < nu - 1) m.ib.insert(m.ib.end(), {a, c, d});
  }
  return m;
}

int main() {
  {   // Thin broad slabs: the door-depth repair must retain the requested local edge bound.
    Mesh m = box(0.5f, 20);
    for (size_t v = 0; v < m.vb.size(); v += 8) {
      m.vb[v] *= 3.06f; m.vb[v + 1] *= .024f; m.vb[v + 2] *= 1.553f;
      vec3 n = normalize(vec3(m.vb[v + 3]/3.06f, m.vb[v + 4]/.024f, m.vb[v + 5]/1.553f));
      m.vb[v + 3] = n.x; m.vb[v + 4] = n.y; m.vb[v + 5] = n.z;
    }
    Mesh s = m; size_t end = s.ib.size();
    const float maxEdge = .20f;
    simplifyMesh(s.vb, s.ib, end, .0008f, .04f, maxEdge);
    float longest = 0.f;
    for (size_t t = 0; t < s.ib.size(); t += 3) for (int k = 0; k < 3; k++) {
      const float* a = &s.vb[size_t(s.ib[t + k])*8];
      const float* b = &s.vb[size_t(s.ib[t + (k + 1)%3])*8];
      longest = std::max(longest, length(vec3(a[0]-b[0], a[1]-b[1], a[2]-b[2])));
    }
    CHECK(longest <= maxEdge + 1e-5f, "thin slab exceeded its edge bound: %g", longest);
    CHECK(deviation(m, s) < 1e-4f, "thin slab geometry changed during edge-bounded simplification");
    CHECK(inward(s) == 0, "thin slab acquired inward faces");
    printf("thin door: %zu -> %zu triangles, longest edge %.4f m\n", m.ib.size()/3, s.ib.size()/3, longest);
  }
  {   // the box: its flat faces collapse; the corners, the edges and the painted panel's border stay exact
    Mesh m = box(0.5f, 40), s = m;
    size_t end = s.ib.size();
    simplifyMesh(s.vb, s.ib, end, 0.001f);
    printf("box: %zu -> %zu triangles, %zu -> %zu vertices\n", m.ib.size() / 3, s.ib.size() / 3, m.vb.size() / 8, s.vb.size() / 8);
    CHECK(s.ib.size() * 4 < m.ib.size(), "the box kept %zu of %zu triangles", s.ib.size() / 3, m.ib.size() / 3);
    float dev = deviation(m, s);
    CHECK(dev < 1e-4f, "the box's surface moved by %g m", dev);
    CHECK(inward(s) == 0, "the box has %d inward faces", inward(s));
    // the panel: every vertex of the original id-2 region's border is still there
    std::map<std::tuple<int, int, int>, int> have;
    for (size_t v = 0; v < s.vb.size() / 8; v++) have[std::make_tuple((int)lroundf(s.vb[v * 8] * 1e4f), (int)lroundf(s.vb[v * 8 + 1] * 1e4f), (int)lroundf(s.vb[v * 8 + 2] * 1e4f))] = 1;
    int lost = 0;
    for (size_t v = 0; v < m.vb.size() / 8; v++) {
      if (m.vb[v * 8 + 6] != 2.f) continue;
      bool border = false;   // (an id-2 vertex next to an id-1 one)
      for (size_t t = 0; t < m.ib.size() / 3 && !border; t++) {
        bool inT = false, other = false;
        for (int k = 0; k < 3; k++) { uint32_t x = m.ib[t * 3 + k]; if (x == v) inT = true; else if (m.vb[x * 8 + 6] != 2.f) other = true; }
        border = inT && other;
      }
      if (border && !have.count(std::make_tuple((int)lroundf(m.vb[v * 8] * 1e4f), (int)lroundf(m.vb[v * 8 + 1] * 1e4f), (int)lroundf(m.vb[v * 8 + 2] * 1e4f)))) lost++;
    }
    CHECK(lost == 0, "%d of the painted panel's border vertices were collapsed", lost);
  }
  {   // the sphere: curved everywhere, so it keeps triangles in proportion to the bound, and stays within it
    Mesh m = sphere(0.5f, 64, 128), s = m;
    size_t end = s.ib.size();
    simplifyMesh(s.vb, s.ib, end, 0.001f);
    printf("sphere: %zu -> %zu triangles\n", m.ib.size() / 3, s.ib.size() / 3);
    CHECK(s.ib.size() * 2 < m.ib.size(), "the sphere kept %zu of %zu triangles", s.ib.size() / 3, m.ib.size() / 3);
    float dev = deviation(m, s);
    CHECK(dev < 0.0015f, "the sphere's surface moved by %g m (bound 1 mm)", dev);
    CHECK(inward(s) == 0, "the sphere has %d inward faces", inward(s));
  }
  {   // the kept range: triangles past triEnd are left exactly as they were
    Mesh m = box(0.5f, 10), s = m;
    size_t end = (s.ib.size() / 3 / 2) * 3;
    std::vector<uint32_t> tail(m.ib.begin() + end, m.ib.end());
    simplifyMesh(s.vb, s.ib, end, 0.001f);
    CHECK(s.ib.size() - end == tail.size(), "the kept range changed size: %zu -> %zu", tail.size(), s.ib.size() - end);
    bool same = true;
    for (size_t i = 0; i < tail.size() && same; i++)
      for (int c = 0; c < 3; c++) same = same && m.vb[tail[i] * 8 + c] == s.vb[s.ib[end + i] * 8 + c];
    CHECK(same, "the kept range's vertices moved");
  }
  if (fails) { printf("%d check(s) failed\n", fails); return 1; }
  printf("mesh simplifier: all checks passed\n");
  return 0;
}
