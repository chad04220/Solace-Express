// Writes every procedural material layer (materials.cpp) as two binary PPMs and a PGM - what tools/pack_materials.py
// matches each scanned layer's average colour, roughness and relief to, and for looking at side by side.
//   material_dump OUTPUT_DIR   ->  NN_name_alb.ppm (as stored: sqrt of linear), NN_name_nrm.ppm (x, y, height),
//                                  NN_name_ra.ppm (roughness, AO, 0)
#include "materials.h"
#include <cstdio>
#include <string>
#include <vector>

static void ppm(const std::string& path, const std::vector<uint8_t>& px, int c0, int c1, int c2) {
  FILE* f = fopen(path.c_str(), "wb");
  if (!f) { printf("can't write %s\n", path.c_str()); return; }
  fprintf(f, "P6 %d %d 255\n", kMatTS, kMatTS);
  for (size_t i = 0; i < (size_t)kMatTS * kMatTS * 4; i += 4) {
    uint8_t rgb[3] = {c0 < 0 ? (uint8_t)0 : px[i + c0], c1 < 0 ? (uint8_t)0 : px[i + c1], c2 < 0 ? (uint8_t)0 : px[i + c2]};
    fwrite(rgb, 1, 3, f);
  }
  fclose(f);
}

int main(int argc, char** argv) {
  if (argc < 2) { printf("usage: material_dump OUTPUT_DIR\n"); return 1; }
  const std::string dir = argv[1];
  std::vector<uint8_t> alb((size_t)kMatTS * kMatTS * 4), nrm(alb.size()), ra(alb.size());
  for (int l = 0; l < kMatLayers; l++) {
    materialProcLayer(l, alb.data(), nrm.data());
    for (size_t i = 0; i < alb.size(); i += 4) { ra[i] = alb[i + 3]; ra[i + 1] = nrm[i + 3]; }
    const std::string base = dir + "/" + (l < 10 ? "0" : "") + std::to_string(l) + "_" + materialName(l) + "_";
    ppm(base + "alb.ppm", alb, 0, 1, 2); ppm(base + "nrm.ppm", nrm, 0, 1, 2); ppm(base + "ra.ppm", ra, 0, 1, -1);
  }
  printf("%d layers written to %s\n", kMatLayers, dir.c_str());
  return 0;
}
