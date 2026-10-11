// Lossless, driver-independent aircraft geometry. No GL or native struct serialization.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace aircraftAsset {
using Digest = std::array<uint8_t, 32>;
inline constexpr uint32_t kFormatVersion = 1;
inline constexpr uint32_t kHeaderBytes = 192;
inline constexpr uint32_t kFullQuality = 1;
inline constexpr uint32_t kCanonicalProfile = 1;
inline constexpr uint64_t kMaxFileBytes = 512ull * 1024 * 1024;
// The model and slot are semantic identities, never paths supplied by an asset.
struct Identity { Digest geometry{}, source{}; uint32_t model = 0, slot = 0; };
struct MeshData {
  std::vector<float> vertices, hull; // hull ends in the exact 0/1 eye-in-moving-volume flag
  std::vector<uint32_t> indices, parts; // parts: type, float count, index count, float bit words, indices
  uint32_t fineStart = 0;
};
Digest sha256(const void* bytes, size_t size);
std::string hex(const Digest& digest);
Digest fromHex(const std::string& text); // invalid input yields the all-zero (invalid) digest
std::string filename(const Identity& identity);
bool valid(const MeshData& data, std::string& error);
// Failed reads/decodes leave out unchanged. All metadata, payload bytes and counts are checked before publication.
bool encode(const Identity& identity, const MeshData& data, std::vector<uint8_t>& out, std::string& error);
bool decode(const std::vector<uint8_t>& bytes, const Identity& expected, MeshData& out, std::string& error);
bool read(const std::string& path, const Identity& expected, MeshData& out, std::string& error);
bool write(const std::string& path, const Identity& identity, const MeshData& data, std::string& error);
// Format v1, all numbers little endian; floats are IEEE-754 binary32 with their bits unchanged:
// magic[8]="SEAMSH01"; u32 version@8, headerBytes@12, fullQuality@16, algorithm@20,
// model@24, slot@28, profile@32, reserved0@36, vertexFloats@40, indices@44,
// hullFloats@48, fineStart@52, partWords@56, reserved0@60; u64 payloadBytes@64;
// geometrySHA256[32]@72, fileSHA256[32]@104, sourceSHA256[32]@136, reserved0[24]@168.
// File SHA256 covers header and payload with bytes104..135 zeroed. Payload order:
// vertices, indices, hull, parts. No padding, compression, transforms or lossy encoding.
} // namespace aircraftAsset
