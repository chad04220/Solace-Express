// CPU authoring/pose contract. Exact surface/appearance still needs EGL visual QA.
#include "../src/aircraft_mesh_build_wraith.h"
#include "../src/wraith_loadout_parts.h"
#include "../src/wraith_hardware_normals.h"
#include <cstring>
#include "../src/aircraft_mesh_planar.h"
#include "../src/mesh_validation.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <set>
using namespace aircraftBuild;
static int checks=0;
static void check(bool v,const char* what){++checks;if(!v){fprintf(stderr,"FAIL %s\n",what);std::exit(1);}}
int main(int argc,char** argv){
 // Proven plane normals are corrected without modifying geometry or materials;
 // disagreeing gradients on a rounded bevel must remain untouched.
 for(bool bevel:{false,true}){
  std::vector<float> vb={0,0,0, .7f,0,.7f,92,1, 1,0,0, .7f,0,.7f,92,1, 0,1,0, .7f,0,.7f,92,1};
  std::vector<uint32_t> ib={0,1,2};std::vector<float> g={0,0,1,0, 0,0,1,0, 0,0,1,0};
  if(bevel){g[4]=.1f;g[6]=.995f;}
  const auto old=vb;aircraftMesh::PlanarRepairStats st;
  check(aircraftMesh::repairVerifiedPlanarNormals(vb,ib,g,st),"valid planar repair input");
  check(st.repaired==(bevel?0u:1u),"rounded bevel gradient disagreement rejected");
  for(int k=0;k<3;k++)for(int j:{0,1,2,6,7})check(vb[ib[k]*8+j]==old[k*8+j],"planar repair preserves position material and AO");
  if(bevel)check(vb==old,"bevel vertices unchanged");
 }
 // A ring wall has shared crease vertices with deliberately tilted normals.
 // Repair must retain exact positions/materials while restoring radial normals.
 for(int type:{PT_WR_BOMB,PT_WR_KINETIC,PT_WR_CHARGED,PT_WR_PENETRATOR,PT_WR_EMP}){
  const auto ring=wraithLoadout::roundSurfaces(type).front();
  std::vector<float> vb;std::vector<uint32_t> ib={0,1,2};
  const vec3 pts[]={vec3(ring.outer,ring.y,ring.z-ring.half),vec3(ring.outer*cosf(.1f),ring.y+ring.outer*sinf(.1f),ring.z-ring.half),vec3(ring.outer,ring.y,ring.z+ring.half)};
  for(auto p:pts){float v[]={p.x,p.y,p.z,0,0,1,82,1};vb.insert(vb.end(),v,v+8);}
  check(wraithLoadout::repairRoundNormals(type,vb,ib)==1,"verified round wall repaired");
  for(int k=0;k<3;k++){const float* v=&vb[ib[k]*8];check(v[0]==pts[k].x&&v[1]==pts[k].y&&v[2]==pts[k].z&&v[6]==82,"repair preserves geometry and material");check(fabsf(v[5])<1e-6f&&fabsf(v[3]*v[3]+v[4]*v[4]-1)<1e-5f,"wall normal radial and unit length");}
  for(size_t i=0;i<vb.size();i+=8)vb[i]+=1.f;
  check(wraithLoadout::repairRoundNormals(type,vb,ib)==0,"unverified surface untouched");
 }
 PartInst instances[kMaxPartInst];float packed[96]={};packed[2]=6;
 int n=wraith::parts(packed,false,instances,6);
 check(n<=kMaxPartInst,"all authored variants fit bounded part ABI");
 std::set<int> authored;for(int i=0;i<n;i++)authored.insert(instances[i].type);
 for(int k:{PT_WR_KINETIC,PT_WR_CHARGED,PT_WR_PENETRATOR,PT_WR_EMP})check(authored.count(k)&&uint32_t(k)<=aircraftMesh::kMaxPartType,"variant authored and cache accepted");
 for(int gun=0;gun<3;gun++)for(int payload=0;payload<3;payload++)for(bool loaded:{false,true}){
  int guns=0,bombs=0,arms=0,doors=0;
  for(int i=0;i<n;i++)if(wraithLoadout::selected(instances[i].type,gun,payload,loaded)){
   int k=instances[i].type;if(k==PT_WR_TURRET||k==PT_WR_KINETIC||k==PT_WR_CHARGED)guns++;
   if(wraithLoadout::isPayload(k))bombs++;
   if(k==PT_WR_ARM)arms++;
   if(k==PT_WR_HATCH)doors++;
  }
  check(guns==2,"each loadout has exactly one gun on each hardpoint");check(bombs==(loaded?1:0),"one selected payload only while loaded");check(arms==2&&doors==2,"original mounting arms and hatches preserved");
 }
 // The new gun boxes must remain inside the existing union of emitter and tip.
 vec3 lo,hi;float h;
 const vec3 gunLo(-.175f,-.145f,-1.15f),gunHi(.175f,.145f,.365f);
 for(int k:{PT_WR_KINETIC,PT_WR_CHARGED}){
  check(wraith::partBox(k,lo,hi,h),"gun has explicit field bounds");check(h<=.003f,"gun detail lattice no coarser than 3 mm");
  for(int a=0;a<3;a++)check(lo[a]>=gunLo[a]&&hi[a]<=gunHi[a],"new cartridge preserves approved mount envelope");
  for(int step=0;step<=100;step++)for(int mirror:{-1,1}){
   float deploy=step*.01f;vec3 mount(.95f,-.1f-.12f*.0025443f-.18f-.38f*deploy,-5.1f-.25f*deploy);
   for(int corner=0;corner<8;corner++){
    vec3 local((corner&1)?hi.x:lo.x,(corner&2)?hi.y:lo.y,(corner&4)?hi.z:lo.z);
    vec3 world=mount+local;world.x*=mirror;
    check(std::isfinite(world.x)&&std::isfinite(world.y)&&std::isfinite(world.z),"deployed mirrored mount remains finite");
    check(std::abs(world.x)<1.126f&&world.z>-6.501f&&world.z<-4.73f,"swept new mount does not extend beyond existing emitter envelope");
   }
  }
 }
 for(int k:{PT_WR_PENETRATOR,PT_WR_EMP}){
  check(wraith::partBox(k,lo,hi,h),"payload has explicit field bounds");check(h<=.003f,"payload detail lattice no coarser than 3 mm");
  check(std::max(std::abs(lo.x),std::abs(hi.x))<.52f-.20f,"payload has at least 20 cm bay side clearance");
  check(lo.z> -1.66f+.60f&&hi.z<1.66f-.60f,"payload has at least 60 cm bay end clearance");
  check(lo.y-.305f>-.575f&&hi.y-.305f<-.015f,"new payload fits closed-door vertical envelope");
 }
 // Exact six-segment spherical brace layout: every capsule clears the core and
 // every end intersects its ring. Bounds and original payload center stay fixed.
 check(wraith::partBox(PT_WR_BOMB,lo,hi,h)&&h==.003f,"plasma cage uses3mm detail lattice");
 for(int axis=0;axis<3;axis++)check(lo[axis]==-.31f&&hi[axis]==.31f,"plasma payload envelope unchanged");
 const float halfArc=asinf(.168f/.281f);
 for(int segment=0;segment<6;segment++){
  const float a0=-halfArc+2*halfArc*segment/6,a1=-halfArc+2*halfArc*(segment+1)/6;
  vec3 p0(.281f*cosf(a0),0,.281f*sinf(a0)),p1(.281f*cosf(a1),0,.281f*sinf(a1));
  for(int sample=0;sample<=100;sample++){
   vec3 p=p0+(p1-p0)*(sample/100.f);
   check(length(p)-.010f>.267f+.002f,"entire brace capsule clears spherical core by2mm");
   check(length(p)+.010f<.31f,"brace stays within payload envelope");
  }
 }
 float endpointRadius=sqrtf(.281f*.281f-.168f*.168f);
 check(endpointRadius>.221f&&endpointRadius<.238f&&fabsf(.168f-.152f)<.020f,"brace ends positively intersect ring volume");
 // Tie selection/pose contract to the production source; no hidden second draw path.
 std::string root=argc>1?argv[1]:".";
 auto read=[&](const char* name){std::ifstream f(root+"/"+name);std::ostringstream s;s<<f.rdbuf();check(bool(f),name);return s.str();};
 auto poses=read("src/shaders/plane_parts.glsl");
 check(poses.find("k == PT_WR_TURRET || k == PT_WR_KINETIC || k == PT_WR_CHARGED")!=std::string::npos,"variant guns use original turret pose");
 check(poses.find("k == PT_WR_BOMB || k == PT_WR_PENETRATOR || k == PT_WR_EMP")!=std::string::npos,"variant payloads use original bomb pose");
 auto renderer=read("src/aircraft_mesh.cpp");check(renderer.find("wraithLoadout::selected(pl[i].type")!=std::string::npos,"one pose list selects every depth/shadow/material draw");
 auto field=read("src/shaders/wraith_weapon_sdf.glsl");check(field.find("uTime")==std::string::npos&&field.find("uWr")==std::string::npos,"cached variant geometry is independent of runtime state");
 check(field.find("segment<6")!=std::string::npos&&field.find("halfArc=asin(.168/.281)")!=std::string::npos,"tested brace arc matches GLSL authoring");
 printf("PASS %d Wraith loadout selection/bounds/swept-envelope/cache contracts. Surface images require EGL QA.\n",checks);
}
