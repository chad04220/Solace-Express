// Reproducible metre-scale audit: production flyable model packing, actual entity meshes,
// and generated city instances. Read-only: no changes to aircraft or scenery geometry.
#include "../src/models.h"
#include "../src/aircraft.h"
#include "../src/scenery.h"
#include "../src/entity_mesh.h"
#include <cstdio>
#include <set>
#include <fstream>
#include <sstream>
#include <string>

namespace {
int failures = 0;
void check(bool ok, const char* label) { if (!ok) { std::printf("FAIL: %s\n", label); failures++; } }
struct Range { float lo=1e9f,hi=-1e9f; void add(float x){lo=std::min(lo,x);hi=std::max(hi,x);} };
struct Bounds { Range x,y,z,doorY; };
}
int main(int argc, char** argv) {
  const std::string root = argc > 1 ? argv[1] : ".";
  auto source = [&](const char* path) { std::ifstream f(root + "/" + path); std::ostringstream text; text << f.rdbuf(); check(f.good() || f.eof(), "scale-contract source is readable"); return text.str(); };
  const std::string entityVS=source("src/shaders/ent_vs.glsl"),entityFS=source("src/shaders/ent_fs2.glsl"),planeVS=source("src/shaders/plane_mesh_vs.glsl");
  check(entityVS.find("vec3 lp = posed*iB.xyz;")!=std::string::npos && entityVS.find(": lp; vLN")!=std::string::npos,"entity facade coordinates are post-scale metres");
  check(entityFS.find("float row = clamp(floor((q.y - 2.0)/3.4 + 0.5)")!=std::string::npos &&
        entityFS.find("vec2(side ? 3.1 : 2.8,3.4)")!=std::string::npos,
        "apartment front and side openings retain their 3.4 metre nominal floor pitch");
  check(entityFS.find("vec3 nominal = lp/vScale;")!=std::string::npos &&
        entityFS.find("float module = 1.55, start = 0.0, base = 0.3;")!=std::string::npos &&
        entityFS.find("glassU = dot(nominal,vec3(-n0.z,0.0,n0.x));")!=std::string::npos &&
        entityFS.find("module = 1.45; start = -2.323761; base = 6.9;")!=std::string::npos &&
        entityFS.find("float fl = fract((nominal.y - base)/3.7);")!=std::string::npos,
        "office/skyscraper floor grids align with their authored metre-scale geometry");
  // Upstream's rigid-piece rendering routes the intact pose through local aliases.
  // Preserve that path: unscaled authored positions, then rotation and translation.
  check(planeVS.find("vec3 pos = aPos, nrm = aNrm;")!=std::string::npos &&
        planeVS.find("mat3 rot = uRot; vec3 at = uPos;")!=std::string::npos &&
        planeVS.find("vW = rot*pos + at;")!=std::string::npos,
        "production intact aircraft mesh placement has rotation and translation, no size multiplier");
  puts("STREETS: 7m carriageway, 1.5m sidewalk each side; cities84x56m, town140x112m, village168x112m blocks");
  puts("NOMINAL_FACADE_PITCH: houses2.9m, bungalow/farm3.0m, townhouse3.2m, apartment3.4m, tower3.6m, office/skyscraper3.7m; actual instance pitches are bounded below");
  puts("FLYABLE FLEET: main-wing span in physics vs production packed model; fuselage stations exclude attached hardware");
  for (int i=0;i<=kWraith;i++) {
    const AircraftSpec& spec=kAircraft[i]; const ModelDef& model=kModels[i];
    Plane p; p.spec=&spec; float packed[96];packModel(spec,i,p.gearHeight(),packed);
    float span=packed[9*4]*2.f, fuselage=model.st[7][0]-model.st[0][0];
    check(std::abs(span-spec.span)<.0001f,"authored/packed main-wing span matches flight physics");
    check(packed[0]==spec.fusLen,"nominal fuselage length reaches the renderer unchanged");
    std::printf("AIRCRAFT,%s,physics_span_m=%.3f,packed_wing_span_m=%.3f,nominal_fuselage_m=%.3f,station_extent_m=%.3f\n",spec.name,spec.span,span,spec.fusLen,fuselage);
  }
  std::vector<EVert> vertices; EntMeshRange meshes[EK_COUNT];buildEntityMeshes(vertices,meshes);
  Bounds bounds[EK_COUNT];
  for(int kind=0;kind<EK_COUNT;kind++) for(int i=meshes[kind].first[0];i<meshes[kind].first[0]+meshes[kind].count[0];i++){
    const EVert& v=vertices[i];auto&b=bounds[kind];b.x.add(v.px);b.y.add(v.py);b.z.add(v.pz);if(int(v.part+.5f)==P_DOOR)b.doorY.add(v.py);
  }
  const auto& car=bounds[EK_CAR];
  std::printf("CAR,actual_mesh_width_m=%.3f,length_m=%.3f,height_m=%.3f\n",car.x.hi-car.x.lo,car.z.hi-car.z.lo,car.y.hi-car.y.lo);
  check(car.x.hi-car.x.lo>1.5f && car.x.hi-car.x.lo<2.1f,"car mesh width is ordinary passenger-car scale");
  check(car.z.hi-car.z.lo>4.f && car.z.hi-car.z.lo<5.2f,"car mesh length is ordinary passenger-car scale");
  g_world.build();
  Range width[EK_COUNT],depth[EK_COUNT],height[EK_COUNT],yscale[EK_COUNT],door[EK_COUNT]; int count[EK_COUNT]={};
  std::set<std::pair<int,int>> chunks;
  for(int t=0;t<kNumTowns;t++) {
    const Town& town=kTowns[t];
    if(town.kind==2)check(g_communityPlans[t].blockX*LOT==84.f&&g_communityPlans[t].blockZ*LOT==56.f,"city block dimensions remain 84 by 56 metres");
    for(int cz=Scenery::chunkOf(town.z-town.r);cz<=Scenery::chunkOf(town.z+town.r);cz++)
      for(int cx=Scenery::chunkOf(town.x-town.r);cx<=Scenery::chunkOf(town.x+town.r);cx++)chunks.insert({cx,cz});
  }
  for(auto key:chunks){auto*c=g_scenery.ensure(key.first,key.second,1);if(!c)continue;
    for(int kind=EK_HOUSE;kind<=EK_GASSTATION;kind++)for(uint32_t i=c->off[kind];i<c->off[kind+1];i++) {
      const Ent&e=c->ents[i];if(communityAt(e.x,e.z)<0)continue;count[kind]++;const auto&b=bounds[kind];
      width[kind].add((b.x.hi-b.x.lo)*e.sx);depth[kind].add((b.z.hi-b.z.lo)*e.sz);height[kind].add(b.y.hi*e.sy);yscale[kind].add(e.sy);
      if(b.doorY.hi>b.doorY.lo)door[kind].add((b.doorY.hi-b.doorY.lo)*e.sy);
    }
  }
  puts("BUILDINGS: measured LOD0 above-base roof/antenna height; footprint includes authored overhangs/porches");
  for(int kind=EK_HOUSE;kind<=EK_GASSTATION;kind++)if(count[kind]){
    if(kind!=EK_SILO && kind!=EK_WATERTOWER)
      check(yscale[kind].lo>=.92f && yscale[kind].hi<=1.08f,"placed habitable architecture stays within its authorized vertical scale");
    std::printf("BUILDING,%s,n=%d,width_m=%.2f..%.2f,depth_m=%.2f..%.2f,height_m=%.2f..%.2f,vertical_scale=%.3f..%.3f",kEntInfo[kind].name,count[kind],width[kind].lo,width[kind].hi,depth[kind].lo,depth[kind].hi,height[kind].lo,height[kind].hi,yscale[kind].lo,yscale[kind].hi);
    if(door[kind].hi>door[kind].lo)std::printf(",door_mesh_height_m=%.2f..%.2f",door[kind].lo,door[kind].hi);puts("");
    if(kind==EK_OFFICE || kind==EK_SKYSCRAPER) {
      const float lo=3.7f*yscale[kind].lo,hi=3.7f*yscale[kind].hi;
      check(lo>=3.404f-.0001f && hi<=3.996f+.0001f,"actual office/skyscraper storeys stay within 3.404 to 3.996 metres");
      std::printf("CURTAIN_WALL,%s,nominal_floor_pitch_m=3.700,actual_floor_pitch_m=%.3f..%.3f\n",kEntInfo[kind].name,lo,hi);
    }
  }
  // Both actual mesh modules and shader windows must stay at real-world metre scale.
  if(count[EK_APARTMENT]){
    check(yscale[EK_APARTMENT].lo==1.f && yscale[EK_APARTMENT].hi==1.f,"fixed apartment balcony modules are not vertically stretched");
    check(std::abs(door[EK_APARTMENT].lo-2.6f)<.001f && std::abs(door[EK_APARTMENT].hi-2.6f)<.001f,"apartment entrances remain 2.6 metres high");
    std::printf("APARTMENT,shader_window_pitch_m=3.400,mesh_balcony_pitch_m=%.3f..%.3f,door_height_m=%.3f..%.3f\n",3.4f*yscale[EK_APARTMENT].lo,3.4f*yscale[EK_APARTMENT].hi,door[EK_APARTMENT].lo,door[EK_APARTMENT].hi);
  }
  const float vx=25000.f,vz=-9000.f,centre=g_world.height(vx,vz);
  check(centre>1778.f && centre<1782.f,"volcano floor stays at its measured 1780 metre elevation");
  for(float radius:{0.f,145.f,340.f,520.f,600.f}){
    Range elevation;
    for(int i=0;i<360;i++){float a=i*DEG;elevation.add(g_world.height(vx+radius*cosf(a),vz+radius*sinf(a)));}
    std::printf("VOLCANO,radius_m=%.0f,elevation_m=%.2f..%.2f,height_above_centre_m=%.2f..%.2f\n",radius,elevation.lo,elevation.hi,elevation.lo-centre,elevation.hi-centre);
  }
  std::printf("VOLCANO_RATIO,floor_diameter_m=290,rim_diameter_m=680,outer_blend_diameter_m=1040,audit_disk_diameter_m=1200,cone_footprint_diameter_m=10400,rim_in_Kestrel_spans=%.2f,rim_in_Meridian_spans=%.2f\n",680.f/kAircraft[0].span,680.f/kAircraft[5].span);
  std::printf("CHECKS,%d failures\n",failures);
  return failures?1:0;
}
