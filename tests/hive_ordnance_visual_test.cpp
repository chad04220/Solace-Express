#include "../src/hive_ordnance_sprites.h"
#include "../src/hive_combat.h"
#include <cstdio>
#include <cstdlib>
static void require(bool v,const char* s){if(!v){fprintf(stderr,"FAIL %s\n",s);std::exit(1);}}
int main(){
 static_assert(kMaxHiveOrdnance>=hive::MaxProjectiles,"Every simulation projectile requires an independent visual slot");
 FrameParams f{};f.camPos=vec3(0,1,0);f.camRight=vec3(1,0,0);f.camUp=vec3(0,1,0);f.camBack=vec3(0,0,1);f.hiveOrdnanceN=hive::MaxProjectiles;
 f.fx.beams=16;f.fx.bombs=8; // maximum unrelated FX cannot hide any ordnance
 for(int i=0;i<f.hiveOrdnanceN;i++){auto& p=f.hiveOrdnance[i];p.tail=vec3(float(i),2,-30);p.head=p.tail+vec3(0,0,-2);p.kind=0;}
 std::vector<SpriteVert> body,glow;buildHiveOrdnanceSprites(f,720,body,glow);
 require(body.empty()&&glow.size()==size_t(hive::MaxProjectiles)*18,"96 independent bolts emit 96 cores and trails with legacy FX full");
 // Exactly camera-aligned ribbons have zero projected area; the independent head
 // billboard must retain area for every active shot, including zero-length trails.
 for(bool zero:{false,true}){
  for(auto& p:f.hiveOrdnance){p.head=vec3(0,1,-30);p.tail=zero?p.head:vec3(0,1,-32);}
  buildHiveOrdnanceSprites(f,720,body,glow);
  for(int i=0;i<f.hiveOrdnanceN;i++){
   auto pos=[&](int j){const auto& v=glow[size_t(i)*18+12+j];return vec3(v.x,v.y,v.z);};
   require(fabsf(dot(cross(pos(1)-pos(0),pos(2)-pos(0)),f.camBack))>.001f,"head-on bolt core has nonzero projected area");
  }
 }
 for(int i=0;i<f.hiveOrdnanceN;i++){f.hiveOrdnance[i].kind=1;f.hiveOrdnance[i].tail=f.hiveOrdnance[i].head;}
 buildHiveOrdnanceSprites(f,720,body,glow);require(body.size()==size_t(hive::MaxProjectiles)*6&&glow.empty(),"96 inert bombs all emit opaque bodies, no glow");
 for(auto& v:body)require(std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z),"head-on/zero-length geometry finite");
 for(int i=0;i<f.hiveOrdnanceN;i++)f.hiveOrdnance[i].kind=2;
 buildHiveOrdnanceSprites(f,720,body,glow);require(body.size()==size_t(hive::MaxProjectiles)*6&&glow.size()==size_t(hive::MaxProjectiles)*6,"96 EMPs emit bodies and restrained glow");
 f.hiveOrdnanceN=0;buildHiveOrdnanceSprites(f,720,body,glow);require(body.empty()&&glow.empty(),"empty frame clears prior geometry");
 puts("PASS full-capacity ordnance independent of legacy beam slots; finite billboards and distinct inert/EMP geometry");
}
