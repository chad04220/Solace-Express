// Solace Express - the terrain mesh and the sea for the raster renderer (renderer.h). A quadtree of 32 x 32-quad
// chunks is picked around the camera every frame: a chunk is split while the camera is within kSplit chunk sizes of
// it, from a root chunk covering the world (2500 m cells) down to 0.3 m cells right under the camera. The vertex
// shader (kTerrainVS) sets every vertex on the heightfield and morphs chunk edges onto the next coarser level, so
// the levels meet without cracks; the fragment shader writes the ground material into the G-buffer. The sea is a
// radial grid at y = 0 that rides along with the camera.
#include "renderer.h"
#include "shaders.h"
#include <algorithm>
#include <functional>

namespace {
const float kSplit = 2.6f;          // split while the camera is nearer than this many chunk sizes
const int kMinLevel = -7;           // finest chunk: 0.305 m cells, 9.8 m across
const float kTerrainFar = 90000.f;  // the projection's far plane (Renderer::viewProj): a copy of the islands beyond it is not visited
const int kChunkVerts = TP_CHUNK * TP_CHUNK * 6;
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
    glGenBuffers(1, &iboTerrain); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, iboTerrain);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * sizeof(uint16_t), idx.data(), GL_STATIC_DRAW);
  }
  glBindVertexArray(0);
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

// The chunks for this view, into terrInst (origin x, z, cell size): the quadtree from the world down, culled against
// the frustum, split by distance. Chunk height bounds come from the envelope's cell maxima (world.cpp), one level-0
// texel for every sub-texel chunk.
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
  // L: the chunk's level (cells of T * 2^L); x0, z0: its origin (m)
  std::function<void(int, float, float)> visit = [&](int L, float x0, float z0) {
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
    if (L > kMinLevel && dx * dx + dz * dz + dy * dy < kSplit * kSplit * S * S) {
      float h = S * 0.5f;
      for (int k = 0; k < 4; k++) visit(L - 1, x0 + (k & 1) * h, z0 + (k >> 1) * h);
    } else terrInst.insert(terrInst.end(), {x0, z0, cs, 0.f});
  };
  // the islands, and their copies across the map's seam as far as the view reaches (the far side's islands on the
  // horizon: the vertex shader's heights wrap, so a copy's chunks are the islands' own ground)
  for (int kz = -1; kz <= 1; kz++) for (int kx = -1; kx <= 1; kx++) {
    copyX = kx * WRAP_SPAN; copyZ = kz * WRAP_SPAN;
    const float ex = std::max(std::max(-WORLD_HALF + copyX - cam.x, cam.x - (WORLD_HALF + copyX)), 0.f), ez = std::max(std::max(-WORLD_HALF + copyZ - cam.z, cam.z - (WORLD_HALF + copyZ)), 0.f);
    if ((kx != 0 || kz != 0) && ex * ex + ez * ez > kTerrainFar * kTerrainFar) continue;
    visit(TP_LEVELS - 1, -WORLD_HALF + copyX, -WORLD_HALF + copyZ);
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
