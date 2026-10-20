// Somnium Volandi - PNG files: an RGB8 image, bottom row first (as glReadPixels returns it), compressed losslessly.
// Each row is filtered the way that leaves the least to code (PNG's None, Sub, Up or Paeth), then deflated: repeats
// found through a hash chain (LZ77), coded with deflate's fixed Huffman table. Photo mode's shots and the diagnostics'
#pragma once
#include <cstdint>
#include <vector>

std::vector<uint8_t> encodePNG(int w, int h, const std::vector<uint8_t>& rgbBottomUp);
bool writePNG(const char* path, int w, int h, const std::vector<uint8_t>& rgbBottomUp);
