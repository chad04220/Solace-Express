// The aircraft hull must be a closed, consistently wound surface with no T-junctions, or a ray can slip through it and
// skip the airframe: for random voxel grids (solid voxels, empty ones and ones split into sub-voxels) every directed
// edge of the mesh has to be matched by the same edge the other way round.
#include "../src/hull_mesh.h"
#include <cstdio>
#include <map>
#include <tuple>

static int check(int n1, bool fine, std::vector<uint8_t>& state, std::unordered_map<int, uint64_t>& mask, int& tris) {
  std::vector<float> tri;
  const float org = -3.f, U = fine ? 0.0625f : 0.25f;
  hullFaces(n1, org, U, fine, state, mask, tri);
  tris = (int)tri.size() / 9;
  typedef std::tuple<int, int, int> P;
  std::map<std::pair<P, P>, int> edges;
  auto lat = [&](size_t v) { return P((int)lroundf((tri[v * 3] - org) / U), (int)lroundf((tri[v * 3 + 1] - org) / U), (int)lroundf((tri[v * 3 + 2] - org) / U)); };
  for (size_t t = 0; t < tri.size() / 9; t++)
    for (int e = 0; e < 3; e++) edges[{lat(t * 3 + e), lat(t * 3 + (e + 1) % 3)}]++;
  int bad = 0;
  for (auto& kv : edges) {
    auto it = edges.find({kv.first.second, kv.first.first});
    if (it == edges.end() || it->second != kv.second) bad++;
  }
  return bad;
}

int main() {
  Rng r(31);
  int fails = 0;
  for (int trial = 0; trial < 300; trial++) {
    int n1 = 3 + trial % 7;
    bool fine = trial % 3 != 0;
    std::vector<uint8_t> state((size_t)n1 * n1 * n1, 0);
    std::unordered_map<int, uint64_t> mask;
    float pSolid = r.range(0.1f, 0.7f), pSplit = fine ? r.range(0.f, 0.5f) : 0.f;
    for (int i = 0; i < n1 * n1 * n1; i++) {
      float u = r.range(0.f, 1.f);
      if (u < pSplit) {
        uint64_t m = 0; float fill = r.range(0.05f, 0.95f);
        for (int b = 0; b < 64; b++) if (r.range(0.f, 1.f) < fill) m |= 1ull << b;
        if (m == 0) continue;
        if (m == ~0ull) { state[i] = 1; continue; }
        state[i] = 2; mask[i] = m;
      } else if (u < pSplit + pSolid) state[i] = 1;
    }
    int tris = 0, bad = check(n1, fine, state, mask, tris);
    if (bad) { printf("  !! trial %d (%d^3, %s): %d unmatched edges of %d triangles\n", trial, n1, fine ? "fine" : "coarse", bad, tris); fails++; }
  }
  printf("hull_test: 300 random voxel grids, %s\n", fails ? "FAIL" : "every mesh closed and consistently wound");
  return fails ? 1 : 0;
}
