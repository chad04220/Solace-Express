// Checksummed, independently versioned enemy assets. All header/payload words are
// little endian; no native structs, unchecked sizes or partial writes are trusted.
#pragma once
#include "enemy_fleet.h"
#include "mesh_validation.h"
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace enemyCache {
constexpr uint32_t kMagic=0x324d4e45u,kVersion=2,kHeaderBytes=40;
constexpr uint64_t kMaxPayload=256ull*1024*1024;
constexpr uint32_t kMaxFloats=32u*1024*1024,kMaxIndices=32u*1024*1024;
constexpr uint64_t kHashStart=1469598103934665603ull;
inline uint64_t byteHash(uint64_t h,uint8_t b){return (h^b)*1099511628211ull;}
inline uint64_t wordHash(uint64_t h,uint64_t v,int n=8){for(int i=0;i<n;i++)h=byteHash(h,uint8_t(v>>(i*8)));return h;}
inline uint64_t textHash(uint64_t h,const std::string& s){h=wordHash(h,s.size());for(unsigned char c:s)h=byteHash(h,c);return h;}
inline uint64_t fingerprint(const EnemyCraftSpec& spec,int type,const std::string& field,const std::string& sampler,
                            const std::string& algorithm,const std::string& driver){
 uint64_t h=wordHash(kHashStart,kVersion);h=wordHash(h,uint32_t(type));
 for(const auto& s:{std::string(spec.id?spec.id:""),std::string(spec.role?spec.role:""),field,sampler,algorithm,driver})h=textHash(h,s);
 const float values[]={spec.boundsMin.x,spec.boundsMin.y,spec.boundsMin.z,spec.boundsMax.x,spec.boundsMax.y,spec.boundsMax.z,spec.radius,spec.hoverHeight};
 for(float f:values){uint32_t bits;std::memcpy(&bits,&f,4);h=wordHash(h,bits,4);}return h;
}
inline bool sizes(uint64_t nf,uint64_t ni){return nf>0&&ni>0&&nf%8==0&&ni%3==0&&nf<=kMaxFloats&&ni<=kMaxIndices&&4*(nf+ni)<=kMaxPayload;}
inline void put(std::array<uint8_t,kHeaderBytes>& h,size_t at,uint64_t v,int n){for(int j=0;j<n;j++)h[at+j]=uint8_t(v>>(8*j));}
inline uint64_t get(const std::array<uint8_t,kHeaderBytes>& h,size_t at,int n){uint64_t v=0;for(int j=0;j<n;j++)v|=uint64_t(h[at+j])<<(8*j);return v;}
inline uint64_t payloadHash(uint64_t fp,const std::vector<float>& v,const std::vector<uint32_t>& ix){
 uint64_t h=wordHash(wordHash(wordHash(kHashStart,fp),v.size()),ix.size());
 for(float f:v){uint32_t w;std::memcpy(&w,&f,4);h=wordHash(h,w,4);}for(uint32_t w:ix)h=wordHash(h,w,4);return h;
}
inline bool load(const std::string& path,uint64_t expected,std::vector<float>& vertices,std::vector<uint32_t>& indices){
 if(path.empty())return false;
 std::error_code ec;uint64_t fileBytes=std::filesystem::file_size(path,ec);if(ec||fileBytes<kHeaderBytes||fileBytes>kHeaderBytes+kMaxPayload)return false;
 FILE* f=std::fopen(path.c_str(),"rb");if(!f)return false;
 std::array<uint8_t,kHeaderBytes> header{};
 bool ok=std::fread(header.data(),1,header.size(),f)==header.size();
 const uint64_t fp=get(header,8,8),nf=get(header,24,4),ni=get(header,28,4),sum=get(header,32,8);
 ok=ok&&get(header,0,4)==kMagic&&get(header,4,4)==kVersion&&fp==expected&&sizes(nf,ni)&&get(header,16,8)==4*(nf+ni)&&fileBytes==kHeaderBytes+4*(nf+ni);
 if(!ok){std::fclose(f);return false;}
 std::vector<float> v;std::vector<uint32_t> ix;
 try{v.resize(size_t(nf));ix.resize(size_t(ni));}catch(const std::bad_alloc&){std::fclose(f);return false;}
 uint64_t hash=wordHash(wordHash(wordHash(kHashStart,fp),nf),ni);
 std::array<uint8_t,16384> bytes{};
 for(uint64_t at=0;ok&&at<nf+ni;){
  const size_t words=size_t(std::min<uint64_t>(bytes.size()/4,nf+ni-at));
  ok=std::fread(bytes.data(),4,words,f)==words;
  if(!ok)break;
  for(size_t q=0;q<words;q++){
   uint32_t w=0;for(int b=0;b<4;b++){uint8_t x=bytes[q*4+b];w|=uint32_t(x)<<(b*8);hash=byteHash(hash,x);}
   if(at+q<nf)std::memcpy(&v[size_t(at+q)],&w,4);else ix[size_t(at+q-nf)]=w;
  }at+=words;
 }
 // Reject concurrent truncation/appending, malformed geometry and valid-looking bit flips.
 ok=ok&&hash==sum&&std::fgetc(f)==EOF&&aircraftMesh::valid(v,ix);std::fclose(f);
 if(!ok)return false;
 vertices.swap(v);indices.swap(ix);return true;
}
inline bool save(const std::string& path,uint64_t fp,const std::vector<float>& vertices,const std::vector<uint32_t>& indices){
 if(path.empty()||!sizes(vertices.size(),indices.size())||!aircraftMesh::valid(vertices,indices))return false;
 std::array<uint8_t,kHeaderBytes> header{};
 put(header,0,kMagic,4);put(header,4,kVersion,4);put(header,8,fp,8);put(header,16,4ull*(vertices.size()+indices.size()),8);
 put(header,24,vertices.size(),4);put(header,28,indices.size(),4);put(header,32,payloadHash(fp,vertices,indices),8);
 static std::atomic<uint64_t> serial{0};
 const std::string temp=path+".tmp-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-"+std::to_string(serial++);
 FILE* f=std::fopen(temp.c_str(),"wb");if(!f)return false;
 bool ok=std::fwrite(header.data(),1,header.size(),f)==header.size();std::array<uint8_t,16384> bytes{};
 uint64_t total=vertices.size()+indices.size();
 for(uint64_t at=0;ok&&at<total;){
  const size_t words=size_t(std::min<uint64_t>(bytes.size()/4,total-at));
  for(size_t q=0;q<words;q++){
   uint32_t w;if(at+q<vertices.size())std::memcpy(&w,&vertices[size_t(at+q)],4);else w=indices[size_t(at+q-vertices.size())];
   for(int b=0;b<4;b++)bytes[q*4+b]=uint8_t(w>>(8*b));
  }
  ok=std::fwrite(bytes.data(),4,words,f)==words;at+=words;
 }
 ok=std::fclose(f)==0&&ok;
 if(ok){
#ifdef _WIN32
  ok=MoveFileExW(std::filesystem::path(temp).c_str(),std::filesystem::path(path).c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
#else
  std::error_code renameError;std::filesystem::rename(temp,path,renameError);ok=!renameError;
#endif
 }
 if(!ok){std::error_code ignored;std::filesystem::remove(temp,ignored);}return ok;
}
}
