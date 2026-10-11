// Solace Express - the terrain mesh and the sea for the raster renderer (renderer.h). A quadtree of 32 x 32-quad
// chunks is picked around the camera every frame: a chunk is split while the camera is within kSplit chunk sizes of
// it, from a root chunk covering the world (2500 m cells) down to 0.3 m cells right under the camera - and along the
// graded roads further still, until its cells are no wider than half the road's platform or a small fraction of the
// distance (the road's level bed and its banks drawn as they are, not a cell's slope across them). The vertex
// shader (kTerrainVS) sets every vertex on the heightfield and morphs chunk edges onto the next coarser level, so
// the levels meet without cracks; skirts hang from every chunk's edges where a road's detail meets the plain levels
// beside it. The fragment shader writes the ground material into the G-buffer. The sea is a radial grid at y = 0
// that rides along with the camera.
#include "renderer.h"
#include "shaders.h"
#include <algorithm>
#include <functional>

namespace {
const float kSplit = 2.6f;          // split while the camera is nearer than this many chunk sizes
const int kMinLevel = -7;           // finest chunk: 0.305 m cells, 9.8 m across
const float kTerrainFar = 90000.f;  // the projection's far plane (Renderer::viewProj): a copy of the islands beyond it is not visited
const int kChunkVerts = TP_CHUNK * TP_CHUNK * 6 + 4 * TP_CHUNK * 6;   // (the grid's cells, then the skirts' quads)
// along the graded roads: cells no wider than half the narrowest road's platform there, or than this much of the
// distance out to 1.2 km (as fine as the plain levels a sixth of the way), the allowance growing with the distance
// beyond - by 2.4 km no finer than the plain levels (further off a road's bed is a line, its paint the pixel's own)
const float kRoadDetail = 0.005f, kRoadDetailNear = 1200.f;
float roadCellAllowed(float d) { return kRoadDetail * d * std::max(1.f, d / kRoadDetailNear); }
// the distance at which a cell of size cs is no longer finer than allowed (its inverse)
float roadCellDistance(float cs) {
  const float x = cs / kRoadDetail;
  return x <= kRoadDetailNear ? x : sqrtf(x * kRoadDetailNear);
}
}

bool Renderer::compileTerrainMesh(const std::function<void()>& step) {
  std::string e;
  setCompileStage("terrain surface");
  progTerrain = linkProgramCached(terrainVSAssembly(""), terrainFSAssembly(""), e);
  if (!progTerrain) { error = "Terrain shader: " + e; return false; }
  if (step) step();
  setCompileStage("ocean surface");
  progWater = linkProgramCached(waterVSAssembly(""), waterFSAssembly(""), e);
  if (!progWater) { error = "Water shader: " + e; return false; }
  if (step) step();
  return true;
}

void Renderer::initTerrainMesh() {
  glGenVertexArrays(1, &vaoTerrain); glBindVertexArray(vaoTerrain);
  glGenBuffers(1, &vboTerrainInst); glBindBuffer(GL_ARRAY_BUFFER, vboTerrainInst);
  glBufferData(GL_ARRAY_BUFFER, 4096 * sizeof(float) * 4, nullptr, GL_STREAM_DRAW);
  glEnableVertexAttribArray(0); glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 16, (void*)0); glVertexAttribDivisor(0, 1);
  // the chunk's grid as indexed triangles: 33 x 33 vertices shared by the 2048 triangles (the same six corners per
  // cell, in the same order and winding, as the plain vertex list had), so the vertex shader's height and normal
  // samples run once per grid point instead of once per triangle corner (5.6x fewer: Codex's performance review)
  {
    std::vector<uint16_t> idx; idx.reserve(kChunkVerts);
    for (int cell = 0; cell < TP_CHUNK * TP_CHUNK; cell++) for (int k = 0; k < 6; k++) {
      int ox = (k == 2 || k == 3 || k == 5) ? 1 : 0, oy = (k == 1 || k == 4 || k == 5) ? 1 : 0;
      int gx = cell % TP_CHUNK + ox, gy = cell / TP_CHUNK + oy;
      idx.push_back((uint16_t)(gy * (TP_CHUNK + 1) + gx));
    }
    // the skirts: a quad down from each edge's every cell, its lower corners the vertices numbered after the grid's
    // (33 x 33 + edge x 33 + k: the edge's k-th vertex, dropped - kTerrainVS)
    const int NG = (TP_CHUNK + 1) * (TP_CHUNK + 1);
    auto edgeVertex = [&](int e, int k) { return e == 0 ? k : e == 1 ? k * (TP_CHUNK + 1) + TP_CHUNK : e == 2 ? TP_CHUNK * (TP_CHUNK + 1) + k : k * (TP_CHUNK + 1); };
    for (int e = 0; e < 4; e++)
      for (int k = 0; k < TP_CHUNK; k++) {
        const uint16_t a = (uint16_t)edgeVertex(e, k), b = (uint16_t)edgeVertex(e, k + 1);
        const uint16_t c = (uint16_t)(NG + e * (TP_CHUNK + 1) + k + 1), d = (uint16_t)(NG + e * (TP_CHUNK + 1) + k);
        for (uint16_t v : {a, b, c, a, c, d}) idx.push_back(v);
      }
    glGenBuffers(1, &iboTerrain); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, iboTerrain);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * sizeof(uint16_t), idx.data(), GL_STATIC_DRAW);
  }
  glBindVertexArray(0);
  buildRoadLod();
  // the sea grid: rings from 2 m to 300 km, each 6% wider, 192 segments; the first ring closes on a centre vertex.
  // (Fine because depth is logarithmic per vertex and only interpolated between: across a triangle 15% deep it was off
  // by 0.3% of the distance, a metre at 300 m - enough for the sea floor, held just under the sea (terrain_vs), to show
  // through it in ovals. At 6% it's 0.05%)
  const int R = 205, SEG = 192;
  std::vector<float> v; std::vector<unsigned> idx;
  v.push_back(0); v.push_back(0);
  for (int r = 0; r < R; r++) {
    float rad = 2.f * powf(1.06f, (float)r);
    for (int s = 0; s < SEG; s++) { float a = 2.f * PI * s / SEG; v.push_back(rad * cosf(a)); v.push_back(rad * sinf(a)); }
  }
  auto at = [&](int r, int s) { return 1u + (unsigned)r * SEG + (unsigned)((s + SEG) % SEG); };
  for (int s = 0; s < SEG; s++) { idx.push_back(0); idx.push_back(at(0, s)); idx.push_back(at(0, s + 1)); }
  for (int r = 0; r + 1 < R; r++)
    for (int s = 0; s < SEG; s++) {
      idx.push_back(at(r, s)); idx.push_back(at(r + 1, s)); idx.push_back(at(r + 1, s + 1));
      idx.push_back(at(r, s)); idx.push_back(at(r + 1, s + 1)); idx.push_back(at(r, s + 1));
    }
  waterIdx = (int)idx.size();
  glGenVertexArrays(1, &vaoWater); glBindVertexArray(vaoWater);
  glGenBuffers(1, &vboWater); glBindBuffer(GL_ARRAY_BUFFER, vboWater);
  glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(float), v.data(), GL_STATIC_DRAW);
  glEnableVertexAttribArray(0); glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 8, (void*)0);
  glGenBuffers(1, &iboWater); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, iboWater);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * sizeof(unsigned), idx.data(), GL_STATIC_DRAW);
  glBindVertexArray(0);
}

void Renderer::buildRoadLod() {
  const int N0 = HM_N; const float T = HM_TEXEL;
  roadLod.assign(1, std::vector<uint8_t>((size_t)N0 * N0, 0));
  std::vector<uint8_t>& L0 = roadLod[0];
  auto cell = [&](float v) { return std::clamp((int)floorf((v + WORLD_HALF) / T), 0, N0 - 1); };
  for (const RoadSegment& s : g_world.roadGrid.segs) {
    if (s.flags & (RS_BRIDGE | RS_NOGRADE)) continue;
    const float P = roadSpec(s.cls).halfPlatform, R = P + 20.f;
    // (where the grading leaves the ground much as it was - a road across level land, its bed the ground itself - the
    // plain levels draw it as well as any)
    bool shaped = false;
    for (int q = 0; q <= 4 && !shaped; q++) {
      const float t = q / 4.f, x = s.ax + (s.bx - s.ax) * t, z = s.az + (s.bz - s.az) * t, hr = s.ah + (s.bh - s.ah) * t;
      const float dx = s.bx - s.ax, dz = s.bz - s.az, l = std::max(hypotf(dx, dz), 1e-3f), nx = -dz / l * (P + 10.f), nz = dx / l * (P + 10.f);
      shaped = fabsf(g_world.naturalHeight(x, z, 6) - hr) > 0.5f || fabsf(g_world.naturalHeight(x + nx, z + nz, 6) - g_world.naturalHeight(x - nx, z - nz, 6)) > 1.f;
    }
    if (!shaped) continue;
    const uint8_t v = (uint8_t)std::clamp(ceilf(P), 1.f, 255.f);
    const float dx = s.bx - s.ax, dz = s.bz - s.az, L2 = std::max(dx * dx + dz * dz, 1e-6f);
    for (int j = cell(std::min(s.az, s.bz) - R); j <= cell(std::max(s.az, s.bz) + R); j++)
      for (int i = cell(std::min(s.ax, s.bx) - R); i <= cell(std::max(s.ax, s.bx) + R); i++) {
        const float cx = -WORLD_HALF + (i + 0.5f) * T, cz = -WORLD_HALF + (j + 0.5f) * T;
        const float t = std::clamp(((cx - s.ax) * dx + (cz - s.az) * dz) / L2, 0.f, 1.f);
        if (hypotf(s.ax + dx * t - cx, s.az + dz * t - cz) > R + T * 0.7072f) continue;
        uint8_t& c = L0[(size_t)j * N0 + i]; if (!c || v < c) c = v;
      }
  }
  for (int L = 1; (N0 >> L) >= 1; L++) {
    const int n = N0 >> L, pn = n * 2;
    const std::vector<uint8_t>& P = roadLod[L - 1];
    std::vector<uint8_t> C((size_t)n * n, 0);
    for (int j = 0; j < n; j++) for (int i = 0; i < n; i++) {
      uint8_t m = 0;
      for (int q = 0; q < 4; q++) { const uint8_t v = P[(size_t)(2 * j + (q >> 1)) * pn + 2 * i + (q & 1)]; if (v && (!m || v < m)) m = v; }
      C[(size_t)j * n + i] = m;
    }
    roadLod.push_back(std::move(C));
  }
}

// The chunks for this view, into terrInst (origin x, z, cell size, the distance its parent stops splitting at - 0: the
// plain levels'): the quadtree from the world down, culled against the frustum, split by distance, and along the
// graded roads further. Chunk height bounds come from the envelope's cell maxima (world.cpp), one level-0 texel for
// every sub-texel chunk.
void Renderer::selectTerrainChunks(const FrameParams& fp) {
  const int N = HM_N; const float T = HM_TEXEL;
  vec3 cam = fp.camPos;
  float rim = 0.f;
  for (int i = 0; i < fp.wreck.craterN; i++) rim += 0.22f * fabsf(fp.wreck.crater[i][3]) + 0.3f;
  float rootTop = 0.f; for (float m : g_world.tpM[TP_LEVELS - 1]) rootTop = std::max(rootTop, m);
  mat4 vpc = viewProj(fp);
  float pl[5][4];
  for (int k = 0; k < 5; k++) {
    int r = k / 2; float sg = (k & 1) ? -1.f : 1.f;
    for (int c = 0; c < 4; c++) pl[k][c] = (k == 4 ? vpc(3, c) + vpc(2, c) : 1.01f * vpc(3, c) + sg * vpc(r, c));
  }
  auto inView = [&](float x0, float x1, float y0, float y1, float z0, float z1) {
    if (fp.pano > 0.f) return true;   // (a panorama sees most of the way round)
    for (int k = 0; k < 5; k++) {
      float x = pl[k][0] >= 0 ? x1 : x0, y = pl[k][1] >= 0 ? y1 : y0, z = pl[k][2] >= 0 ? z1 : z0;
      if (pl[k][0] * x + pl[k][1] * y + pl[k][2] * z + pl[k][3] < 0.f) return false;
    }
    return true;
  };
  terrInst.clear();
  float copyX = 0.f, copyZ = 0.f;   // the copy of the islands being visited (its offset: the map repeats, world.h WRAP_HALF)
  // L: the chunk's level (cells of T * 2^L); x0, z0: its origin (m); byRoad: its parent split for a road's sake
  std::function<void(int, float, float, bool)> visit = [&](int L, float x0, float z0, bool byRoad) {
    float cs = T * ldexpf(1.f, L), S = cs * TP_CHUNK;
    // height bound: the envelope's cell maximum at the chunk's size (L >= 0), or of the texel it lies in (L < 0)
    int ox = (int)floorf((x0 - copyX + WORLD_HALF) / T + 0.01f), oz = (int)floorf((z0 - copyZ + WORLD_HALF) / T + 0.01f);
    int Lm = std::max(L, 0) + 5; float top;
    if (Lm >= TP_LEVELS) top = rootTop;
    else { int n = N >> Lm; int i = std::clamp(ox >> Lm, 0, n - 1), j = std::clamp(oz >> Lm, 0, n - 1); top = g_world.tpM[Lm][(size_t)j * n + i]; }
    top += rim;
    if (!inView(x0, x0 + S, -800.f, top + 1.f, z0, z0 + S)) return;
    float dx = std::max(0.f, std::max(x0 - cam.x, cam.x - (x0 + S))), dz = std::max(0.f, std::max(z0 - cam.z, cam.z - (z0 + S)));
    float dy = std::max(0.f, cam.y - top);
    const float d2 = dx * dx + dz * dz + dy * dy;
    // the narrowest graded road across the chunk (the chunk spans one cell at level L + 5; a smaller one lies in one texel)
    float road = 0.f;
    if (!roadLod.empty()) {
      const int m = std::clamp(L + 5, 0, (int)roadLod.size() - 1), n = N >> m;
      const int i = ox >> m, j = oz >> m;
      if (i >= 0 && j >= 0 && i < n && j < n) road = roadLod[m][(size_t)j * n + i];
    }
    const bool plain = d2 < kSplit * kSplit * S * S;
    const bool forRoad = !plain && road > 0.f && cs > std::max(0.5f * road, roadCellAllowed(sqrtf(d2)));
    if (L > kMinLevel && (plain || forRoad)) {
      float h = S * 0.5f;
      for (int k = 0; k < 4; k++) visit(L - 1, x0 + (k & 1) * h, z0 + (k >> 1) * h, forRoad);
    } else {
      // (a chunk with a road on it there for the road's sake morphs onto its parent's grid by the distance the parent
      // stops splitting for the road; the rest - plain, or a road's neighbour - by the plain levels')
      const float merge = byRoad && road > 0.f ? roadCellDistance(2.f * cs) : 0.f;
      terrInst.insert(terrInst.end(), {x0, z0, cs, merge});
    }
  };
  // the islands, and their copies across the map's seam as far as the view reaches (the far side's islands on the
  // horizon: the vertex shader's heights wrap, so a copy's chunks are the islands' own ground)
  for (int kz = -1; kz <= 1; kz++) for (int kx = -1; kx <= 1; kx++) {
    copyX = kx * WRAP_SPAN; copyZ = kz * WRAP_SPAN;
    const float ex = std::max(std::max(-WORLD_HALF + copyX - cam.x, cam.x - (WORLD_HALF + copyX)), 0.f), ez = std::max(std::max(-WORLD_HALF + copyZ - cam.z, cam.z - (WORLD_HALF + copyZ)), 0.f);
    if ((kx != 0 || kz != 0) && ex * ex + ez * ez > kTerrainFar * kTerrainFar) continue;
    visit(TP_LEVELS - 1, -WORLD_HALF + copyX, -WORLD_HALF + copyZ, false);
  }
  terrChunks = (int)terrInst.size() / 4;
}

// Draws the terrain and the sea into the bound G-buffer (depth-tested against what is there already)
void Renderer::drawTerrainMesh(const FrameParams& fp) {
  if (!progTerrain || !vaoTerrain) return;
  selectTerrainChunks(fp);
  if (terrInst.empty()) return;
  glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS); glDepthMask(GL_TRUE);
  glDisable(GL_BLEND); glDisable(GL_CULL_FACE);
  mat4 vp = viewProj(fp), view = viewMat(fp);
  GLuint p = progTerrain;
  setRT(p, fp);
  glUniformMatrix4fv(U(p, "uVP"), 1, GL_FALSE, vp.m);
  glUniformMatrix4fv(U(p, "uPanoView"), 1, GL_FALSE, view.m);
  glUniform2f(U(p, "uPano"), fp.pano, fp.panoTanY);
  glUniform2f(U(p, "uJit"), jitX, jitY);
  glUniform1f(U(p, "uLogC"), 2.f / log2f(40000.f + 1.f));
  glUniform1f(U(p, "uSplit"), kSplit);
  glBindVertexArray(vaoTerrain); glBindBuffer(GL_ARRAY_BUFFER, vboTerrainInst);
  glBufferData(GL_ARRAY_BUFFER, terrInst.size() * sizeof(float), terrInst.data(), GL_STREAM_DRAW);
  if (!(dbgOff & kProbeTerrain)) glDrawElementsInstanced(GL_TRIANGLES, kChunkVerts, GL_UNSIGNED_SHORT, nullptr, terrChunks);
  // the sea
  if (progWater && vaoWater && fp.camPos.y > -0.5f) {
    p = progWater;
    setRT(p, fp);
    glUniformMatrix4fv(U(p, "uVP"), 1, GL_FALSE, vp.m);
    glUniformMatrix4fv(U(p, "uPanoView"), 1, GL_FALSE, view.m);
    glUniform2f(U(p, "uPano"), fp.pano, fp.panoTanY);
    glUniform2f(U(p, "uJit"), jitX, jitY);
    glUniform1f(U(p, "uLogC"), 2.f / log2f(40000.f + 1.f));
    glActiveTexture(GL_TEXTURE0 + 9); glBindTexture(GL_TEXTURE_2D_ARRAY, texWaves); glUniform1i(U(p, "uWaves"), 9);
    glUniform3f(U(p, "uWaveRms"), waveRms[0], waveRms[1], waveRms[2]);
    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(vaoWater);
    glDrawElements(GL_TRIANGLES, waterIdx, GL_UNSIGNED_INT, nullptr);
  }
  glBindVertexArray(0);
  glDisable(GL_DEPTH_TEST);
}
