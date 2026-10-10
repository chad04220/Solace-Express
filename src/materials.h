#pragma once
#include <cstdint>

// The material texture layers (materials.cpp): kMatLayers layers of kMatTS^2 RGBA texels each
const int kMatTS = 512, kMatLayers = 30;
const char* materialName(int l);                               // the layer's file name: assets/materials/NN_<name>_*.jpg
void materialProcLayer(int l, uint8_t* alb, uint8_t* nrm);     // the procedural layer (kMatTS^2 * 4 bytes each)

// Close-range environment scans only. Keep the aircraft/cockpit 512 px array unchanged.
constexpr int kEnvMatTS = 2048, kEnvMatLayers = 7;
constexpr int kEnvMaterialSources[kEnvMatLayers] = {0, 5, 8, 12, 11, 9, 25};
constexpr int environmentMaterialLayer(int layer) {
  for (int i = 0; i < kEnvMatLayers; ++i) if (kEnvMaterialSources[i] == layer) return i;
  return -1;
}
constexpr int environmentMaterialSource(int layer) {
  return layer >= 0 && layer < kEnvMatLayers ? kEnvMaterialSources[layer] : -1;
}
