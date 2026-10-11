// Independent, CPU-only parser audit. Handcrafted wire mutations deliberately
// recompute SHA-256 to exercise semantics beyond ordinary corruption detection.
#include "../src/aircraft_mesh_asset.h"
#include "../src/mesh_validation.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>

namespace {
using namespace aircraftAsset;
int checks = 0, failures = 0;
void check(bool ok, const char* message) {
  ++checks;
  if (!ok) { ++failures; std::fprintf(stderr, "FAIL: %s\n", message); }
}
uint32_t bits(float f) { uint32_t w; std::memcpy(&w, &f, 4); return w; }
void put32(std::vector<uint8_t>& b, size_t at, uint32_t w) {
  for (unsigned i = 0; i < 4; ++i) b[at + i] = uint8_t(w >> (8 * i));
}
void put64(std::vector<uint8_t>& b, size_t at, uint64_t w) {
  put32(b, at, uint32_t(w)); put32(b, at + 4, uint32_t(w >> 32));
}
uint32_t get32(const std::vector<uint8_t>& b, size_t at) {
  uint32_t w = 0;
  for (unsigned i = 0; i < 4; ++i) w |= uint32_t(b[at + i]) << (8 * i);
  return w;
}
void resign(std::vector<uint8_t>& b) {
  std::fill(b.begin() + 104, b.begin() + 136, 0);
  const Digest d = sha256(b.data(), b.size());
  std::copy(d.begin(), d.end(), b.begin() + 104);
}
template<class T> bool sameBits(const std::vector<T>& a, const std::vector<T>& b) {
  return a.size() == b.size() && (a.empty() || std::memcmp(a.data(), b.data(), a.size() * sizeof(T)) == 0);
}
bool same(const MeshData& a, const MeshData& b) {
  return sameBits(a.vertices, b.vertices) && sameBits(a.indices, b.indices) &&
         sameBits(a.hull, b.hull) && sameBits(a.parts, b.parts) && a.fineStart == b.fineStart;
}
MeshData fixture() {
  MeshData d;
  // Negative zero and exact non-rounded attributes must survive the wire format.
  d.vertices = {-0.f,0,0, 0,0,1, 3.125f,.5f,
                 1,0,0, 0,0,1, 4.25f,.75f,
                 0,1,0, 0,0,1, 6.5f,1,
                 1,1,0, 0,0,1, 8.25f,.625f};
  const uint32_t smallestSubnormal = 1;
  std::memcpy(&d.vertices[2], &smallestSubnormal, sizeof smallestSubnormal);
  d.indices = {0,1,2, 1,3,2}; d.fineStart = 3;
  d.hull = {0,0,0, 1,0,0, 0,1,0, 1};
  d.parts = {0,24,3};
  for (size_t i = 0; i < 24; ++i) d.parts.push_back(bits(d.vertices[i]));
  d.parts.insert(d.parts.end(), {0,1,2});
  d.parts.insert(d.parts.end(), {46,24,3});
  for (size_t i = 0; i < 24; ++i) d.parts.push_back(bits(d.vertices[i]));
  d.parts.insert(d.parts.end(), {0,1,2});
  return d;
}
}

int main() {
  using namespace aircraftAsset;
  const Identity id{sha256("geometry",8), sha256("source",6), 14, 1};
  const MeshData original = fixture();
  std::string error;
  // Published SHA-256 vectors plus independently derived padding boundaries.
  check(hex(sha256(nullptr,0)) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "SHA-256 empty vector");
  check(hex(sha256("abc",3)) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "SHA-256 abc vector");
  const std::pair<size_t,const char*> padding[] = {
    {55,"9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318"},
    {56,"b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a"},
    {63,"7d3e74a05d7db15bce4ad9ec0658ea98e3f06eeecf16b4c6fff2da457ddc2f34"},
    {64,"ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb"},
    {65,"635361c48bb9eab14198e76ea8ab7f1a41685d6ad62aa9146d301d4f17eb0ae0"},
    {1000,"41edece42d63e8d9bf515a9ba6932e1c20cbc9f5a5d134645adb5db1b9737ea3"}};
  for (const auto& p : padding) { const std::string s(p.first,'a'); check(hex(sha256(s.data(),s.size())) == p.second,"SHA-256 block/padding boundary"); }
  check(fromHex(hex(id.geometry)) == id.geometry, "digest round trip");
  check(fromHex(std::string(64,'Z')) == Digest{} && fromHex("abc") == Digest{}, "invalid hex digest rejected");

  std::vector<uint8_t> encoded;
  check(encode(id, original, encoded, error), "encode full fixture");
  if (encoded.empty()) return 1;
  check(encoded.size() == 192 + 4 * (original.vertices.size()+original.indices.size()+original.hull.size()+original.parts.size()), "wire has no native padding");
  check(get32(encoded,192) == 0x80000000u && encoded[192] == 0 && encoded[195] == 0x80, "IEEE negative-zero bits in little endian");
  check(get32(encoded,192+2*4) == 1, "subnormal coordinate bits survive without float conversion");
  check(get32(encoded,20) == aircraftMesh::kAlgorithmVersion && get32(encoded,52) == 3, "algorithm and coarse/fine boundary serialized");
  MeshData restored;
  check(decode(encoded,id,restored,error) && error.empty() && same(original,restored), "every mesh array preserves exact bits and order");
  std::vector<uint8_t> again;
  check(encode(id,restored,again,error) && again == encoded, "canonical deterministic re-encoding");
  const std::string name = filename(id);
  check(!name.empty() && name.find_first_of("/\\:") == std::string::npos && name.find("..") == std::string::npos, "asset basename cannot traverse directories");

  auto reject = [&](const std::vector<uint8_t>& b, const Identity& expected, const char* what) {
    MeshData out = original; out.fineStart = 6;
    const MeshData before = out; error = "old error";
    const bool ok = decode(b,expected,out,error);
    check(!ok && !error.empty() && error != "old error" && same(before,out), what);
  };
  // Every possible truncated prefix and every single-byte bit flip must fail
  // without mutating the existing destination, including a partially parsed part.
  for (size_t n = 0; n < encoded.size(); ++n)
    reject(std::vector<uint8_t>(encoded.begin(),encoded.begin()+n),id,"truncated prefix rejects atomically");
  for (size_t i = 0; i < encoded.size(); ++i) {
    auto b = encoded; b[i] ^= 0x80; reject(b,id,"single-byte corruption rejects atomically");
  }
  { auto b=encoded; b.push_back(0); reject(b,id,"trailing bytes rejected"); }
  for (unsigned field = 0; field < 4; ++field) {
    Identity wrong=id;
    if(field==0) wrong.model=13;
    if(field==1) wrong.slot=0;
    if(field==2) wrong.geometry[0]^=1;
    if(field==3) wrong.source[0]^=1;
    reject(encoded,wrong,"wrong model/view/geometry/source rejected");
  }
  for (const auto& p : {std::pair<size_t,uint32_t>{8,2},{12,196},{16,0},{20,0},{24,13},{28,0},{32,0},{36,1},{60,1},{168,1},{188,1}}) {
    auto b=encoded; put32(b,p.first,p.second); resign(b); reject(b,id,"invalid resigned version/profile/identity/reserved metadata");
  }
  for (size_t at : {size_t(72),size_t(136)}) { auto b=encoded;b[at]^=1;resign(b);reject(b,id,"resigned identity digest mismatch"); }
  for (const auto& p : {std::pair<size_t,uint32_t>{40,0},{40,31},{44,0},{44,5},{48,0},{48,9},{52,1},{52,9},{56,0},{40,UINT32_MAX},{44,UINT32_MAX},{48,UINT32_MAX},{56,UINT32_MAX}}) {
    auto b=encoded;put32(b,p.first,p.second);resign(b);reject(b,id,"section count/range overflow rejected before allocation");
  }
  { auto b=encoded;for(size_t at:{size_t(40),size_t(44),size_t(48),size_t(56)})put32(b,at,UINT32_MAX);put64(b,64,4ull*4*UINT32_MAX);resign(b);reject(b,id,"combined 32-bit count overflow rejected"); }
  { auto b=encoded;put64(b,64,UINT64_MAX);resign(b);reject(b,id,"64-bit payload overflow rejected"); }

  const size_t vertex=192,index=vertex+original.vertices.size()*4,hull=index+original.indices.size()*4,part=hull+original.hull.size()*4;
  auto badWord = [&](size_t at,uint32_t value,const char* what) {auto b=encoded;put32(b,at,value);resign(b);reject(b,id,what);};
  for(uint32_t f:{0x7fc00000u,0x7f800000u,0xff800000u}) {
    badWord(vertex,f,"resigned nonfinite static attribute rejected");
    badWord(hull,f,"resigned nonfinite hull rejected");
    badWord(part+12,f,"resigned nonfinite rigid-part attribute rejected");
  }
  badWord(vertex+5*4,0,"zero-length static normal rejected");
  badWord(vertex+5*4,bits(2.f),"nonunit static normal rejected");
  badWord(vertex+7*4,bits(-.01f),"negative ambient occlusion rejected");
  badWord(vertex+7*4,bits(1.01f),"out-of-range ambient occlusion rejected");
  badWord(index,uint32_t(original.vertices.size()/8),"static out-of-range index rejected");
  badWord(hull+(original.hull.size()-1)*4,bits(.5f),"nonboolean hull eye flag rejected");
  badWord(part,aircraftMesh::kMaxPartType+1,"unassigned part type rejected");   // (one past the last PT_*)
  badWord(part+4,UINT32_MAX,"oversized part vertex count rejected");
  badWord(part+8,UINT32_MAX,"oversized part index count rejected");
  badWord(part+8,2,"nontriangular part topology rejected");
  badWord(part+12+24*4,3,"part out-of-range index rejected");
  badWord(part+30*4,0,"duplicate rigid-part type rejected");
  badWord(part+12+5*4,0,"zero-length rigid-part normal rejected");
  badWord(part+12+7*4,bits(2.f),"invalid rigid-part AO rejected");
  // Splits at either extreme are legal; moving-volume triangles are optional.
  for(uint32_t split:{0u,6u}) {auto d=original;d.fineStart=split;d.hull={0.f};std::vector<uint8_t>b;check(encode(id,d,b,error)&&decode(b,id,restored,error)&&same(d,restored),"empty moving volume and extreme fine split preserved");}
  {auto d=original;d.parts.clear();std::vector<uint8_t>b;check(encode(id,d,b,error)&&decode(b,id,restored,error)&&same(d,restored),"aircraft without rigid parts preserves empty section");}
  for(unsigned field=0;field<4;++field) {
    auto bad=id;if(field==0)bad.slot=2;if(field==1)bad.model=UINT32_MAX;if(field==2)bad.geometry={};if(field==3)bad.source={};
    auto b=encoded;check(!encode(bad,original,b,error)&&b==encoded&&filename(bad).empty(),"invalid identity cannot publish or create path");
  }
  {auto d=original;d.indices[0]=UINT32_MAX;auto b=encoded;check(!encode(id,d,b,error)&&b==encoded,"invalid encode leaves previous bytes intact");}

  const auto root=std::filesystem::temp_directory_path()/std::filesystem::path("solace-portable-audit-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::error_code ec;std::filesystem::create_directories(root,ec);
  check(!ec,"create isolated file-test directory");
  if(!ec) {
    const auto path=root/name;
    check(write(path.string(),id,original,error),"atomic file write");
    check(read(path.string(),id,restored,error)&&same(original,restored),"file read exact round trip");
    {auto d=original;d.indices[0]=UINT32_MAX;check(!write(path.string(),id,d,error)&&read(path.string(),id,restored,error)&&same(original,restored),"rejected replacement preserves valid destination");}
    // A failed publication must not leave the sibling temporary file behind.
    const auto directoryTarget=root/"occupied";std::filesystem::create_directory(directoryTarget,ec);
    check(!write(directoryTarget.string(),id,original,error)&&std::filesystem::is_directory(directoryTarget),"failed rename preserves destination");
    bool leftover=false;for(const auto& entry:std::filesystem::directory_iterator(root))if(entry.path().filename().string().find(".tmp.")!=std::string::npos)leftover=true;
    check(!leftover,"failed publication removes temporary file");
    // Sparse oversized file uses virtually no disk or memory. Its declared size
    // must be rejected before a 512 MiB allocation or any payload parsing.
    const auto huge=root/"oversized.mesh";
    {std::ofstream f(huge,std::ios::binary);f.seekp(std::streamoff(kMaxFileBytes));f.put('\0');check(bool(f),"create sparse oversized input");}
    restored=original;check(!read(huge.string(),id,restored,error)&&same(original,restored),"oversized sparse file rejected before allocation");
    restored=original;check(!read((root/"missing.mesh").string(),id,restored,error)&&same(original,restored),"missing file leaves output unchanged");
    // Loading never repairs, removes or writes to a corrupt package asset.
    const auto corrupt=root/"corrupt.mesh";
    {auto b=encoded;b.back()^=1;std::ofstream f(corrupt,std::ios::binary);f.write(reinterpret_cast<const char*>(b.data()),std::streamsize(b.size()));}
    restored=original;check(!read(corrupt.string(),id,restored,error)&&same(original,restored)&&std::filesystem::file_size(corrupt)==encoded.size(),"corrupt read leaves package and output unchanged");
    std::filesystem::remove_all(root,ec);check(!ec,"remove isolated test files");
  }
  std::printf("portable_mesh_adversarial: %d checks, %s\n",checks,failures?"FAIL":"PASS");
  return failures?1:0;
}
