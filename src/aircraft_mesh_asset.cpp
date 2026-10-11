#include "aircraft_mesh_asset.h"
#include "mesh_validation.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <limits>
#include <new>
#include <stdexcept>

namespace aircraftAsset {
namespace {
static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559, "mesh assets require IEEE binary32");
static_assert(sizeof(uint32_t) == 4, "mesh assets require 32-bit words");
uint32_t rotr(uint32_t a, unsigned n) { return (a >> n) | (a << (32 - n)); }
class Sha256 {
  uint32_t h[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
  uint8_t block[64]{}; size_t used = 0; uint64_t total = 0;
  void compress() {
    static constexpr uint32_t K[64] = {
      0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
      0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
      0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
      0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
      0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
      0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
      0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
      0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
    uint32_t w[64];
    for (int i=0;i<16;i++) w[i]=(uint32_t(block[i*4])<<24)|(uint32_t(block[i*4+1])<<16)|(uint32_t(block[i*4+2])<<8)|block[i*4+3];
    for (int i=16;i<64;i++) { const uint32_t a=w[i-15],b=w[i-2]; w[i]=w[i-16]+(rotr(a,7)^rotr(a,18)^(a>>3))+w[i-7]+(rotr(b,17)^rotr(b,19)^(b>>10)); }
    uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],q=h[7];
    for (int i=0;i<64;i++) {
      const uint32_t t1=q+(rotr(e,6)^rotr(e,11)^rotr(e,25))+((e&f)^(~e&g))+K[i]+w[i];
      const uint32_t t2=(rotr(a,2)^rotr(a,13)^rotr(a,22))+((a&b)^(a&c)^(b&c));
      q=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;
    }
    h[0]+=a;h[1]+=b;h[2]+=c;h[3]+=d;h[4]+=e;h[5]+=f;h[6]+=g;h[7]+=q;
  }
public:
  void update(const void* bytes, size_t size) {
    const uint8_t* p=static_cast<const uint8_t*>(bytes); total+=size;
    while(size) { const size_t n=std::min(size,64-used); std::memcpy(block+used,p,n); p+=n;size-=n;used+=n;if(used==64){compress();used=0;} }
  }
  Digest finish() {
    const uint64_t bits=total*8; block[used++]=0x80;
    if(used>56){std::fill(block+used,block+64,0);compress();used=0;}
    std::fill(block+used,block+56,0);
    for(int i=0;i<8;i++) block[63-i]=uint8_t(bits>>(8*i));
    compress();Digest out{};
    for(int i=0;i<8;i++) for(int j=0;j<4;j++) out[i*4+j]=uint8_t(h[i]>>(24-j*8));
    return out;
  }
};
bool fail(std::string& e,const char* reason) { e=reason;return false; }
uint32_t u32(const uint8_t* p) { return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24); }
uint64_t u64(const uint8_t* p) { return uint64_t(u32(p))|(uint64_t(u32(p+4))<<32); }
void put32(uint8_t* p,uint32_t x){for(int i=0;i<4;i++)p[i]=uint8_t(x>>(8*i));}
void put64(uint8_t* p,uint64_t x){put32(p,uint32_t(x));put32(p+4,uint32_t(x>>32));}
bool nonzero(const Digest& d){for(uint8_t x:d)if(x)return true;return false;}
bool validIdentity(const Identity& id){return id.model<256 && id.slot<2 && nonzero(id.geometry) && nonzero(id.source);}
Digest fileDigest(const std::vector<uint8_t>& b) {
  Sha256 s; const uint8_t zero[32]{};s.update(b.data(),104);s.update(zero,32);s.update(b.data()+136,b.size()-136);return s.finish();
}
uint64_t payloadSize(const MeshData& d) { return 4ull*(uint64_t(d.vertices.size())+d.indices.size()+d.hull.size()+d.parts.size()); }
}
Digest sha256(const void* bytes,size_t size){Sha256 s;s.update(bytes,size);return s.finish();}
std::string hex(const Digest& digest){static const char* digits="0123456789abcdef";std::string r; r.reserve(64);for(uint8_t b:digest){r+=digits[b>>4];r+=digits[b&15];}return r;}
Digest fromHex(const std::string& text){
  Digest r{};if(text.size()!=64)return r;
  auto digit=[](char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;};
  for(size_t i=0;i<32;i++){const int a=digit(text[i*2]),b=digit(text[i*2+1]);if(a<0||b<0)return Digest{};r[i]=uint8_t(a*16+b);}return r;
}
std::string filename(const Identity& id){if(!validIdentity(id))return {};char p[32];std::snprintf(p,sizeof p,"aircraft_%02u_%u_",id.model,id.slot);return std::string(p)+hex(id.geometry)+".mesh";}
bool valid(const MeshData& d,std::string& error) {
  error.clear();
  if(payloadSize(d)>kMaxFileBytes-kHeaderBytes)return fail(error,"aircraft asset exceeds size limit");
  if(d.vertices.empty()||d.indices.empty()||d.vertices.size()>UINT32_MAX||d.indices.size()>INT32_MAX||d.hull.size()>INT32_MAX||d.parts.size()>UINT32_MAX)return fail(error,"invalid aircraft mesh counts");
  if(d.fineStart%3||d.fineStart>d.indices.size())return fail(error,"invalid fine-patch range");
  if(!aircraftMesh::valid(d.vertices,d.indices))return fail(error,"invalid static mesh");
  if(d.hull.empty()||d.hull.size()%9!=1)return fail(error,"invalid moving hull count");
  for(float f:d.hull)if(!aircraftMesh::finite(f))return fail(error,"nonfinite moving hull");
  if(d.hull.back()!=0.f&&d.hull.back()!=1.f)return fail(error,"invalid moving hull eye flag");
  if(!aircraftMesh::validBlob(d.parts))return fail(error,"invalid rigid-part mesh");
  bool seen[aircraftMesh::kMaxPartType+1]{};
  for(size_t at=0;at<d.parts.size();) {
    const uint32_t type=d.parts[at],nf=d.parts[at+1],ni=d.parts[at+2];
    if(seen[type]||ni>INT32_MAX)return fail(error,"duplicate or oversized rigid part");
    seen[type]=true;at+=3+size_t(nf)+ni;
  }
  return true;
}
bool encode(const Identity& id,const MeshData& d,std::vector<uint8_t>& out,std::string& error) {
  try {
    if(!validIdentity(id))return fail(error,"invalid aircraft asset identity");
    if(!valid(d,error))return false;
    std::vector<uint8_t> b(size_t(kHeaderBytes+payloadSize(d)),0);std::memcpy(b.data(),"SEAMSH01",8);
    put32(b.data()+8,kFormatVersion);put32(b.data()+12,kHeaderBytes);put32(b.data()+16,kFullQuality);put32(b.data()+20,aircraftMesh::kAlgorithmVersion);
    put32(b.data()+24,id.model);put32(b.data()+28,id.slot);put32(b.data()+32,kCanonicalProfile);
    put32(b.data()+40,uint32_t(d.vertices.size()));put32(b.data()+44,uint32_t(d.indices.size()));put32(b.data()+48,uint32_t(d.hull.size()));put32(b.data()+52,d.fineStart);put32(b.data()+56,uint32_t(d.parts.size()));
    put64(b.data()+64,payloadSize(d));std::copy(id.geometry.begin(),id.geometry.end(),b.begin()+72);std::copy(id.source.begin(),id.source.end(),b.begin()+136);
    size_t at=kHeaderBytes;
    auto floats=[&](const std::vector<float>& v){for(float f:v){uint32_t w;std::memcpy(&w,&f,4);put32(b.data()+at,w);at+=4;}};
    auto words=[&](const std::vector<uint32_t>& v){for(uint32_t w:v){put32(b.data()+at,w);at+=4;}};
    floats(d.vertices);words(d.indices);floats(d.hull);words(d.parts);
    const Digest checksum=fileDigest(b);std::copy(checksum.begin(),checksum.end(),b.begin()+104);out.swap(b);return true;
  }catch(const std::bad_alloc&){return fail(error,"aircraft asset allocation failed");}catch(const std::length_error&){return fail(error,"aircraft asset allocation exceeds capacity");}
}
bool decode(const std::vector<uint8_t>& b,const Identity& expected,MeshData& out,std::string& error) {
  error.clear();
  try {
    if(!validIdentity(expected))return fail(error,"invalid expected aircraft identity");
    if(b.size()<kHeaderBytes||b.size()>kMaxFileBytes)return fail(error,"invalid aircraft asset file size");
    const uint8_t* p=b.data();
    if(std::memcmp(p,"SEAMSH01",8)||u32(p+8)!=kFormatVersion||u32(p+12)!=kHeaderBytes||u32(p+20)!=aircraftMesh::kAlgorithmVersion)return fail(error,"unsupported aircraft asset version");
    if(u32(p+16)!=kFullQuality||u32(p+32)!=kCanonicalProfile)return fail(error,"aircraft asset is not the canonical full-quality profile");
    if(u32(p+36)||u32(p+60)||std::any_of(b.begin()+168,b.begin()+192,[](uint8_t v){return v!=0;}))return fail(error,"nonzero reserved aircraft metadata");
    if(u32(p+24)!=expected.model||u32(p+28)!=expected.slot||!std::equal(expected.geometry.begin(),expected.geometry.end(),p+72)||!std::equal(expected.source.begin(),expected.source.end(),p+136))return fail(error,"stale or mismatched aircraft asset identity");
    const uint32_t nv=u32(p+40),ni=u32(p+44),nh=u32(p+48),fine=u32(p+52),np=u32(p+56);
    const uint64_t bytes=4ull*(uint64_t(nv)+ni+nh+np);
    if(bytes!=u64(p+64)||bytes!=b.size()-kHeaderBytes||nv%8||ni%3||nh%9!=1||fine%3||fine>ni||!nv||!ni||ni>INT32_MAX||nh>INT32_MAX)return fail(error,"invalid aircraft asset section sizes");
    const Digest checksum=fileDigest(b);if(!std::equal(checksum.begin(),checksum.end(),p+104))return fail(error,"aircraft asset checksum mismatch");
    MeshData d;d.vertices.resize(nv);d.indices.resize(ni);d.hull.resize(nh);d.parts.resize(np);d.fineStart=fine;size_t at=kHeaderBytes;
    auto floats=[&](std::vector<float>& v){for(float& f:v){uint32_t w=u32(p+at);std::memcpy(&f,&w,4);at+=4;}};
    auto words=[&](std::vector<uint32_t>& v){for(uint32_t& w:v){w=u32(p+at);at+=4;}};
    floats(d.vertices);words(d.indices);floats(d.hull);words(d.parts);
    if(!valid(d,error))return false;
    out=std::move(d);return true;
  }catch(const std::bad_alloc&){return fail(error,"aircraft asset allocation failed");}catch(const std::length_error&){return fail(error,"aircraft asset allocation exceeds capacity");}
}
bool read(const std::string& path,const Identity& expected,MeshData& out,std::string& error) {
  error.clear();
  FILE* f=std::fopen(path.c_str(),"rb");if(!f)return fail(error,"aircraft asset unavailable");
  // The bound is checked against the opened file before any allocation. Read an extra byte to reject growth.
  if(std::fseek(f,0,SEEK_END)){std::fclose(f);return fail(error,"cannot measure aircraft asset");}
  const long n=std::ftell(f);
  if(n<long(kHeaderBytes)||uint64_t(n)>kMaxFileBytes||std::fseek(f,0,SEEK_SET)){std::fclose(f);return fail(error,"invalid aircraft asset file size");}
  try {
    std::vector<uint8_t> b(static_cast<size_t>(n));const bool complete=std::fread(b.data(),1,b.size(),f)==b.size()&&std::fgetc(f)==EOF&&!std::ferror(f);
    const bool closed=std::fclose(f)==0;f=nullptr;
    if(!complete||!closed)return fail(error,"incomplete aircraft asset read");
    return decode(b,expected,out,error);
  }catch(const std::bad_alloc&){if(f)std::fclose(f);return fail(error,"aircraft asset allocation failed");}catch(const std::length_error&){if(f)std::fclose(f);return fail(error,"aircraft asset allocation exceeds capacity");}
}
bool write(const std::string& path,const Identity& id,const MeshData& d,std::string& error) {
  std::vector<uint8_t> b;if(!encode(id,d,b,error))return false;
  const std::string tmp=path+".tmp."+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
  FILE* f=std::fopen(tmp.c_str(),"wb");if(!f)return fail(error,"cannot create aircraft asset temporary file");
  const bool wrote=std::fwrite(b.data(),1,b.size(),f)==b.size();const bool closed=std::fclose(f)==0;std::error_code ec;
  if(!wrote||!closed){std::filesystem::remove(tmp,ec);return fail(error,"cannot write complete aircraft asset");}
  std::filesystem::rename(tmp,path,ec);if(ec){std::filesystem::remove(tmp,ec);return fail(error,"cannot publish aircraft asset");}return true;
}
} // namespace aircraftAsset
