// Solace Express - block compression for the photographed 2K ground materials (renderer.cpp genMaterials): BC3 (DXT5),
// a BC1 colour block for RGB and a BC4 block for alpha in each 4x4, a quarter of RGBA8's bytes. A low-level view along
// the ground - a runway from the cockpit - samples their finest levels on every pixel, anisotropically; uncompressed
// (300 MiB with their mipmaps) that cost the v3.48.0 diagnostics' night scene 4 ms of texture traffic.
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

// The bytes of a w x h level in BC3: 16 a 4x4 block (a level under 4 texels across still takes a whole block)
size_t bc3LevelBytes(int w, int h);
// Encode a w x h RGBA8 image (rows top to bottom, 4 bytes a texel) into BC3 blocks, row of blocks by row (out holds
// bc3LevelBytes(w, h)). Edge blocks of a level that isn't a multiple of 4 repeat its last row and column
void bc3Encode(const uint8_t* rgba, int w, int h, uint8_t* out);
// Decode a BC3 block's 16 texels (RGBA8, row-major) - the reference the test and the encoder's refinement use
void bc3DecodeBlock(const uint8_t* block, uint8_t rgba[64]);
// The next mipmap level of a w x h RGBA8 image: each texel the mean of the 2x2 it covers (as glGenerateMipmap's box
// filter does; an odd edge's last texel taken as it is)
std::vector<uint8_t> mipDown(const uint8_t* rgba, int w, int h, int& nw, int& nh);
