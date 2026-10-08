#pragma once
#include <cstdint>

// The material texture layers (materials.cpp): kMatLayers layers of kMatTS^2 RGBA texels each
const int kMatTS = 512, kMatLayers = 30;
const char* materialName(int l);                               // the layer's file name: assets/materials/NN_<name>_*.jpg
void materialProcLayer(int l, uint8_t* alb, uint8_t* nrm);     // the procedural layer (kMatTS^2 * 4 bytes each)
