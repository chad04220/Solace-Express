#ifdef NDEBUG
#undef NDEBUG
#endif
#include "../src/enemy_mesh_cache.h"
#include <cassert>
#include <fstream>
#include <cstdio>
static std::vector<uint8_t> bytes(const std::string& p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),std::istreambuf_iterator<char>()};}
static void write(const std::string& p,const std::vector<uint8_t>& b){std::ofstream f(p,std::ios::binary|std::ios::trunc);f.write((const char*)b.data(),b.size());}
int main(){
 EnemyCraftSpec s{"fixture","cache regression",vec3(-2),vec3(2),3,1};
 auto stamp=[&](const EnemyCraftSpec& x,const std::string& code="field",const std::string& algo="algorithm"){return enemyCache::fingerprint(x,0,code,"sampler",algo,"driver");};
 uint64_t fp=stamp(s);auto changed=s;changed.boundsMin.x-=.0625f;assert(stamp(changed)!=fp);changed=s;changed.boundsMax.y+=.0625f;assert(stamp(changed)!=fp);changed=s;changed.radius+=1;assert(stamp(changed)!=fp);assert(stamp(s,"field-changed")!=fp&&stamp(s,"field","algorithm2")!=fp);
 const auto folder=std::filesystem::temp_directory_path()/("enemy-cache-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
 std::filesystem::create_directories(folder);const std::string path=(folder/"fixture.bin").string();
 std::vector<float> v={0,0,0,0,1,0,110,1,1,0,0,0,1,0,110,1,0,0,1,0,1,0,110,1};std::vector<uint32_t> ix={0,2,1};
 assert(enemyCache::save(path,fp,v,ix));std::vector<float> out;std::vector<uint32_t> oi;assert(enemyCache::load(path,fp,out,oi)&&out==v&&oi==ix);
 const auto good=bytes(path);
 auto reject=[&](std::vector<uint8_t> b){write(path,b);out={42};oi={43};assert(!enemyCache::load(path,fp,out,oi));assert(out==std::vector<float>{42}&&oi==std::vector<uint32_t>{43});};
 auto b=good;b[enemyCache::kHeaderBytes+2]^=1;reject(b); // still-finite, valid-looking geometry must fail checksum
 b=good;b.back()^=1;reject(b);
 b=good;b.resize(b.size()-1);reject(b);
 b=good;b.push_back(0);reject(b);
 b=good;for(int i=24;i<32;i++)b[i]=255;reject(b); // pathological allocation counts rejected before allocation
 b=good;b[0]^=1;reject(b);
 write(path,good);assert(!enemyCache::load(path,stamp(changed),out,oi));
 // An invalid cache is replaced atomically by a complete rebuilt asset at the same path.
 b=good;b[enemyCache::kHeaderBytes]^=1;write(path,b);assert(!enemyCache::load(path,fp,out,oi));assert(enemyCache::save(path,fp,v,ix));assert(enemyCache::load(path,fp,out,oi));
 auto invalid=v;invalid[3]=0;invalid[4]=0;invalid[5]=0;assert(!enemyCache::save(path,fp,invalid,ix));assert(bytes(path)==good);
 for(const auto& entry:std::filesystem::directory_iterator(folder))assert(entry.path().extension()==".bin");
 assert(!enemyCache::sizes(enemyCache::kMaxFloats+8ull,3));assert(!enemyCache::sizes(8,enemyCache::kMaxIndices+3ull));
 std::filesystem::remove_all(folder);
 puts("PASS enemy cache little-endian roundtrip, authored-bounds/source/algorithm invalidation, checksum bit flips, allocation limits, truncation/trailer rejection, preserved outputs and atomic corruption replacement");
}
