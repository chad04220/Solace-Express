// Immutable pre-redesign runway contract. The fixture was captured by compiling
// the generator against git archive 52317bb, not the current world implementation.
// See fixtures/RUNWAY_BASELINE.md. Do not regenerate it to accept a scenery edit.
#include "../src/world.h"
#include "../src/entities.h"
#include "../src/airport_layout.h"
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cctype>
#include <string>
#include <vector>

#ifndef SOLACE_RUNWAY_GOLDEN
#define SOLACE_RUNWAY_GOLDEN "tests/fixtures/runways_52317bb.golden"
#endif

void airportGroundsCap(float x, float z, float& grounds, float& cap);   // world.cpp
float airportFunnelCeiling(float x, float z);

namespace {
int failures = 0;
size_t sampleCount = 0, fixtureCount = 0, entityCount = 0, chunkCount = 0;
float worstHeight = 0, worstSlope = 0;
constexpr float kHeightTolerance = 0.0005f; // sub-millimetre cross-compiler float allowance
constexpr float kPositionTolerance = 0.006f; // float ULPs at coordinates up to 40 km
constexpr float kSlopeTolerance = 0.00015f;

void check(bool ok, const char* code, const char* what) {
  if (!ok) { if (failures < 40) printf("FAIL %s: %s\n", code, what); ++failures; }
}
float asFloat(uint32_t b) { float f; memcpy(&f,&b,4); return f; }
bool close(float a,float b,float eps) { return std::isfinite(a)&&std::isfinite(b)&&fabsf(a-b)<=eps; }

struct Golden {
  std::string code,name,blurb;
  float x,z,elev,heading,length,width;
  int surface,size,hospital,number[2];
  vec3 dir,end[2];
  std::vector<float> us,vs,height;
  float maxNormal,maxElevation;
  std::vector<AptItem> fixtures;
  Airport airport() const { return {code.c_str(),name.c_str(),x,z,elev,heading,length,width,surface,size,blurb.c_str(),hospital!=0}; }
};

bool readVec(FILE* f,vec3& v) { return fscanf(f,"%a %a %a",&v.x,&v.y,&v.z)==3; }
bool readGolden(FILE* f,Golden& g) {
  char code[32],name[256],blurb[512];
  if (fscanf(f,"%31s %a %a %a %a %a %a %d %d %d %d %d",code,&g.x,&g.z,&g.elev,&g.heading,&g.length,&g.width,
        &g.surface,&g.size,&g.hospital,&g.number[0],&g.number[1])!=12) return false;
  if (fscanf(f," \"%255[^\"]\" \"%511[^\"]\"",name,blurb)!=2) return false;
  g.code=code;g.name=name;g.blurb=blurb;
  if(!readVec(f,g.dir)||!readVec(f,g.end[0])||!readVec(f,g.end[1]))return false;
  size_t nu=0,nv=0,nr=0;
  if(fscanf(f,"%zu %zu",&nu,&nv)!=2||nu==0||nv==0||nu>10000||nv>1000)return false;
  g.us.resize(nu);g.vs.resize(nv);
  for(float& x:g.us)if(fscanf(f,"%a",&x)!=1)return false;
  for(float& x:g.vs)if(fscanf(f,"%a",&x)!=1)return false;
  if(fscanf(f,"%zu %a %a",&nr,&g.maxNormal,&g.maxElevation)!=3||nr>nu*nv)return false;
  for(size_t i=0;i<nr;++i){unsigned count=0,bits=0;if(fscanf(f,"%u %x",&count,&bits)!=2||count==0||count>nu*nv-g.height.size())return false;
    g.height.insert(g.height.end(),count,asFloat(bits));}
  if(g.height.size()!=nu*nv)return false;
  size_t nf=0;if(fscanf(f,"%zu",&nf)!=1||nf>10000)return false;
  g.fixtures.resize(nf);
  for(auto& p:g.fixtures)if(fscanf(f,"%d %a %a %a %a %a %a %a %a",&p.kind,&p.e.x,&p.e.y,&p.e.z,&p.e.yaw,&p.e.sx,&p.e.sy,&p.e.sz,&p.e.seed)!=9)return false;
  // The fixture's kinds are numbered as the enum stood at its capture (runway light 30, PAPI 31); the kinds added
  // since (v3.47's city towers) moved them, never what they are
  for(auto& p:g.fixtures){if(p.kind!=30&&p.kind!=31)return false;p.kind=p.kind==30?EK_RWYLIGHT:EK_PAPI;}
  return true;
}

// This transform uses the frozen direction, so a change in aptWorld cannot move
// the sample grid along with the runway and silently make the comparison pass.
vec3 frozenWorld(const Golden& g,float u,float v) {
  return {g.x+u*g.dir.x-v*g.dir.z,0,g.z+u*g.dir.z+v*g.dir.x};
}
bool intersects(const Golden& g,const Ent& e,int kind,float alongMargin,float acrossMargin) {
  const auto& I=kEntInfo[kind];
  const float du=(e.x-g.x)*g.dir.x+(e.z-g.z)*g.dir.z;
  const float dv=-(e.x-g.x)*g.dir.z+(e.z-g.z)*g.dir.x;
  const float c=cosf(e.yaw),s=sinf(e.yaw);
  const float ax=c*g.dir.x-s*g.dir.z,az=-c*g.dir.z-s*g.dir.x;
  const float hx=I.hx*e.sx,hz=I.hz*e.sz,hu=g.length*.5f+alongMargin,hv=g.width*.5f+acrossMargin;
  // Four separating axes: both runway axes and both entity footprint axes.
  return fabsf(du)<=hu+fabsf(ax)*hx+fabsf(az)*hz &&
         fabsf(dv)<=hv+fabsf(az)*hx+fabsf(ax)*hz &&
         fabsf(du*ax+dv*az)<=hx+hu*fabsf(ax)+hv*fabsf(az) &&
         fabsf(-du*az+dv*ax)<=hz+hu*fabsf(az)+hv*fabsf(ax);
}
void sortFixtures(std::vector<AptItem>& items) {
  std::sort(items.begin(),items.end(),[](const AptItem& a,const AptItem& b){
    if(a.kind!=b.kind)return a.kind<b.kind;
    if(a.e.x!=b.e.x)return a.e.x<b.e.x;
    if(a.e.z!=b.e.z)return a.e.z<b.e.z;
    return a.e.seed<b.e.seed;
  });
}
void bounds(const Golden& g,float uMargin,float vMargin,float& x0,float& z0,float& x1,float& z1) {
  x0=z0=1e9f;x1=z1=-1e9f;
  for(float u:{-g.length*.5f-uMargin,g.length*.5f+uMargin})
    for(float v:{-g.width*.5f-vMargin,g.width*.5f+vMargin}){
      vec3 p=frozenWorld(g,u,v);x0=std::min(x0,p.x);x1=std::max(x1,p.x);z0=std::min(z0,p.z);z1=std::max(z1,p.z);
    }
}

std::string readText(const std::string& path) {
  FILE* f=fopen(path.c_str(),"rb");if(!f)return {};
  std::string s;for(int ch;(ch=fgetc(f))!=EOF;)if(ch!='\r')s.push_back(char(ch));fclose(f);return s;
}
uint64_t textFingerprint(const std::string& s) {
  uint64_t h=14695981039346656037ull;for(unsigned char ch:s)h=(h^ch)*1099511628211ull;return h;
}
std::string compactShader(const std::string& s) {
  std::string out;bool line=false,block=false;
  for(size_t i=0;i<s.size();++i){char c=s[i],next=i+1<s.size()?s[i+1]:0;
    if(line){if(c=='\n')line=false;continue;}
    if(block){if(c=='*'&&next=='/'){block=false;++i;}continue;}
    if(c=='/'&&next=='/'){line=true;++i;continue;}
    if(c=='/'&&next=='*'){block=true;++i;continue;}
    if(!std::isspace((unsigned char)c))out.push_back(c);
  }return out;
}
std::string shaderFunction(const std::string& text,const char* signature) {
  size_t start=text.find(signature);if(start==std::string::npos)return {};
  size_t at=text.find('{',start);if(at==std::string::npos)return {};
  int depth=1;size_t end=at+1;
  for(;end<text.size()&&depth;++end){if(text[end]=='{')++depth;else if(text[end]=='}')--depth;}
  return depth?std::string():text.substr(start,end-start);
}
void testRunwayShader(const char* fixturePath) {
  std::string path=fixturePath;size_t slash=path.find_last_of("/\\");
  std::string dir=slash==std::string::npos?std::string():path.substr(0,slash+1);
  std::string old=readText(dir+"runway_material_52317bb.glsl");
  check(textFingerprint(old)==0x51f48d0f25ad3d99ull,"shader","immutable runway shader fixture changed or missing");
  std::string current=readText(dir+"../../src/shaders/terrain_material.glsl");
  check(!current.empty(),"shader","current terrain shader source unavailable");
  old=compactShader(old);current=compactShader(current);
  // Passing pixel footprint to the redesigned apron affects only the off-runway
  // aptGround branch. It does not authorize any runway surface/marking edits.
  const std::string plumbing="aptGround(ai,uv,pw,surf,size,len,wid,footprint,m)";
  size_t where=current.find(plumbing);
  if(where!=std::string::npos)current.replace(where,plumbing.size(),"aptGround(ai,uv,pw,surf,size,len,wid,m)");
  int before=failures;
  for(const char* signature:{"intairportAt(","floatseg7(","floatrwyDigits(","voidrunwayMaterial("}){
    std::string a=shaderFunction(old,signature),b=shaderFunction(current,signature);
    if(a.empty()||a!=b){printf("  Changed frozen shader function: %s\n",signature);check(false,"shader","runway shape/material/marking source changed");}
  }
  printf("Runway shader: four immutable geometry/material/marking functions %s\n",failures==before?"preserved":"FAILED");
}


void testWorldTerrain(const char* fixturePath) {
  std::string path=fixturePath;size_t slash=path.find_last_of("/\\");
  std::string dir=slash==std::string::npos?std::string():path.substr(0,slash+1);
  const std::string worldPath=dir+"world_height_52317bb.golden";
  std::string text=readText(worldPath);
  if(textFingerprint(text)!=0xc83bbbc2ebf62501ull){check(false,"world","immutable full-world height fixture changed or missing");return;}
  FILE* f=fopen(worldPath.c_str(),"r");if(!f){check(false,"world","full-world height fixture unavailable");return;}
  char magic[64],commit[64];int version=0;size_t count=0;
  bool valid=fscanf(f,"%63s %d %63s %zu",magic,&version,commit,&count)==4&&
    !strcmp(magic,"SOLACE_WORLD_HEIGHT_GOLDEN")&&version==1&&count==17523&&
    !strcmp(commit,"52317bb1f74a5ee9e66e063e2f17d5be786955a0");
  if(!valid){fclose(f);check(false,"world","invalid full-world fixture identity");return;}
  int before=failures;float worst=0;size_t checked=0,craterSamples=0,roadSamples=0;
  for(size_t i=0;i<count;++i){float x,z,expected[3];
    if(fscanf(f,"%a %a %a %a %a",&x,&z,&expected[0],&expected[1],&expected[2])!=5){check(false,"world","malformed full-world fixture");break;}
    // The explicit new volcanic crater is the only authorized physical-terrain
    // exception. Its footprint cannot excuse changes to any airport grounds or
    // approach corridor, even if a future crater location were changed by mistake.
    const float craterX=x-25000.f,craterZ=z+9000.f;
    bool crater=craterX*craterX+craterZ*craterZ<600.f*600.f;
    for(const Airport& a:g_world.airports)if(crater){
      vec2 q=aptLocal(a,vec3(x,0,z));float u=fabsf(q.x),v=fabsf(q.y),d=u-a.length*.5f;
      float du=std::max(0.f,u-a.length*.5f-260.f),dv=std::max(0.f,v-(a.size==2?420.f:240.f));
      if(hypotf(du,dv)<550.f||(d>0&&d<7000.f&&v<250.f+.18f*d+900.f))crater=false;
    }
    if(crater)++craterSamples;
    int at=0;bool graded=false;for(int octaves:{7,8,11}){
      // The second authorized change is the road network's grading (owner's brief, docs/LIVING_ISLANDS_PLAN.md A4):
      // a layer over the natural ground, which itself stays exactly as frozen everywhere outside the crater. Graded
      // ground only within a road's reach, never on any airfield's grounds, and under an approach funnel no higher
      // than the funnel holds the ground (or the ground itself, where that stands higher).
      float natural=g_world.naturalHeight(x,z,octaves),actual=g_world.height(x,z,octaves),delta=fabsf(natural-expected[at]);if(!crater)worst=std::max(worst,delta);
      // Non-flat mountains use compiler/libm-dependent noise arithmetic. Preserve
      // centimetre-scale agreement across platforms; matched builds are bit-identical.
      float tolerance=.01f+3.e-6f*fabsf(expected[at]);
      check(crater?std::isfinite(natural):close(natural,expected[at],tolerance),"world","terrain outside authorized crater changed");
      if(actual!=natural){graded=true;float grounds,cap;airportGroundsCap(x,z,grounds,cap);
        check(roadEdgeDistance(g_world.roadGrid,x,z)<=ROAD_BANK_MAX+1.f,"world","graded ground beyond any road's reach");
        check(grounds==0.f,"world","road grading on an airfield's grounds");
        check(actual<=std::max(natural,airportFunnelCeiling(x,z))+.01f,"world","road grading above an approach funnel's cap");}
      ++at;
    }++checked;roadSamples+=graded;
  }
  char trailing[2];check(fscanf(f,"%1s",trailing)==EOF,"world","unexpected trailing full-world fixture data");fclose(f);
  check(checked==17523,"world","incomplete full-world sample coverage");
  check(roadSamples<checked/20,"world","road grading reaches more than a twentieth of the frozen locations");
  printf("Full-world terrain: %zu frozen locations at 7/8/11 octaves (%zu within explicitly allowed 600 m summit crater, %zu on graded road corridors), max natural height delta elsewhere %.9g m, %s\n",checked,craterSamples,roadSamples,worst,failures==before?"preserved":"FAILED");
}

void testDescriptors(const Golden& g,const Airport& a) {
  const char* code=g.code.c_str();
  check(g.code==a.code&&g.name==a.name&&g.blurb==a.blurb,code,"airport identity changed");
  check(a.x==g.x&&a.z==g.z&&a.elev==g.elev,code,"runway position/elevation changed");
  check(a.heading==g.heading&&a.length==g.length&&a.width==g.width,code,"runway heading/length/width changed");
  check(a.surface==g.surface&&a.size==g.size&&int(a.hospital)==g.hospital,code,"surface/airport attributes changed");
  vec3 d=a.dir();check(close(d.x,g.dir.x,0.000001f)&&close(d.y,g.dir.y,0.000001f)&&close(d.z,g.dir.z,0.000001f),code,"runway direction changed");
  for(int i=0;i<2;++i){vec3 p=a.threshold(i!=0),q=g.end[i];
    check(close(p.x,q.x,kPositionTolerance)&&p.y==q.y&&close(p.z,q.z,kPositionTolerance),code,"runway threshold/endpoint changed");
    check(a.rwyNumber(i!=0)==g.number[i],code,"runway designator changed");
    vec3 w=aptWorld(a,(i?1.f:-1.f)*g.length*.5f,0,g.elev);
    check(close(w.x,q.x,kPositionTolerance)&&w.y==q.y&&close(w.z,q.z,kPositionTolerance),code,"runway transform changed");
  }
}
void testTerrain(const Golden& g,int ai) {
  const char* code=g.code.c_str(); const size_t nv=g.vs.size();std::vector<float> actual(g.height.size());
  size_t k=0;int before=failures;
  for(float u:g.us)for(float v:g.vs){
    vec3 p=frozenWorld(g,u,v);float expected=g.height[k],h7=g_world.height(p.x,p.z,7),h8=g_world.height(p.x,p.z,8),h11=g_world.height(p.x,p.z,11);
    worstHeight=std::max(worstHeight,fabsf(h8-expected));actual[k]=h8;
    check(close(h7,expected,kHeightTolerance)&&close(h8,expected,kHeightTolerance)&&close(h11,expected,kHeightTolerance),code,"dense baseline terrain height changed");
    check(close(g_world.groundHeight(p.x,p.z,7),expected,kHeightTolerance),code,"scenery ground/contact disagrees with runway baseline");
    check(close(std::max(h7,0.f),expected,kHeightTolerance),code,"wheel-contact plane changed");
    vec3 n=g_world.normal(p.x,p.z);
    check(std::isfinite(n.y)&&fabsf(n.x)<=g.maxNormal+kSlopeTolerance&&fabsf(n.z)<=g.maxNormal+kSlopeTolerance&&n.y>0.99999f,code,"runway contact normal/slope changed");
    // Aircraft wheel, belly and water contacts use margins 2, 4 and 30.
    // Skip exact float boundary ambiguity but cover both sides 25 cm apart.
    for(float margin:{0.f,2.f,4.f,30.f}){
      float du=fabsf(u)-g.length*.5f-margin,dv=fabsf(v)-g.width*.5f-margin;
      if(fabsf(du)<0.02f||fabsf(dv)<0.02f)continue;
      int expectedIndex=du<0&&dv<0?ai:-1;
      check(g_world.onRunway(p.x,p.z,margin)==expectedIndex,code,"runway/contact classification boundary changed");
    }
    ++k;
  }
  for(size_t iu=0;iu<g.us.size();++iu)for(size_t iv=0;iv<nv;++iv){
    size_t at=iu*nv+iv;
    auto slope=[&](size_t other,float spacing){
      float delta=((actual[at]-actual[other])-(g.height[at]-g.height[other]))/spacing;
      worstSlope=std::max(worstSlope,fabsf(delta));check(std::isfinite(delta)&&fabsf(delta)<=kSlopeTolerance,code,"baseline longitudinal/transverse gradient changed");
    };
    if(iu)slope(at-nv,g.us[iu]-g.us[iu-1]);if(iv)slope(at-1,g.vs[iv]-g.vs[iv-1]);
  }
  sampleCount+=g.height.size();
  printf("%s: %zu frozen terrain/contact samples, %s\n",code,g.height.size(),failures==before?"preserved":"FAILED");
}
void testScenery(const Golden& g) {
  const char* code=g.code.c_str();float x0,z0,x1,z1;bounds(g,600,600,x0,z0,x1,z1);
  int before=failures;size_t localEntities=0,localChunks=0;
  // Expanded envelope catches entities centred in adjacent chunks whose mesh
  // footprint reaches the runway. Do not test only airportItems(): community
  // buildings, roadside objects, trees and rocks are produced by chunk generation.
  for(int cz=Scenery::chunkOf(z0);cz<=Scenery::chunkOf(z1);++cz)
    for(int cx=Scenery::chunkOf(x0);cx<=Scenery::chunkOf(x1);++cx){
      auto ch=g_scenery.ensure(cx,cz,2);if(!ch)continue;++localChunks;
      for(int kind=0;kind<EK_COUNT;++kind)for(unsigned j=ch->off[kind];j<ch->off[kind+1];++j){
        const Ent& e=ch->ents[j];++localEntities;
        check(std::isfinite(e.x)&&std::isfinite(e.y)&&std::isfinite(e.z)&&std::isfinite(e.yaw)&&e.sx>0&&e.sy>0&&e.sz>0,code,"invalid streamed entity");
        if(kind==EK_RWYLIGHT||kind==EK_PAPI)continue; // existing frangible navigation fixtures
        bool gradedException=kind==EK_FENCE||kind==EK_WINDSOCK;
        if(intersects(g,e,kind,gradedException?0.f:60.f,gradedException?0.f:25.f)){
          if(failures<40)printf("  %s %s at (%.2f, %.2f) intrudes into protected runway\n",code,kEntInfo[kind].name,e.x,e.z);
          check(false,code,"streamed entity footprint intrudes into runway/graded strip");
        }
      }
    }
  std::vector<AptItem> fixtures;bounds(g,430,65,x0,z0,x1,z1);
  for(int cz=Scenery::chunkOf(z0);cz<=Scenery::chunkOf(z1);++cz)
    for(int cx=Scenery::chunkOf(x0);cx<=Scenery::chunkOf(x1);++cx){
      auto ch=g_scenery.ensure(cx,cz,2);if(!ch)continue;
      for(int kind:{EK_RWYLIGHT,EK_PAPI})for(unsigned j=ch->off[kind];j<ch->off[kind+1];++j)
        if(kind==EK_PAPI||ch->ents[j].seed<4.f)fixtures.push_back({kind,ch->ents[j]});
    }
  sortFixtures(fixtures);check(fixtures.size()==g.fixtures.size(),code,"runway/approach/PAPI fixture count changed");
  for(size_t j=0;j<std::min(fixtures.size(),g.fixtures.size());++j){const auto& a=fixtures[j];const auto& b=g.fixtures[j];
    check(a.kind==b.kind&&close(a.e.x,b.e.x,kPositionTolerance)&&close(a.e.y,b.e.y,kHeightTolerance)&&close(a.e.z,b.e.z,kPositionTolerance)&&
      close(a.e.yaw,b.e.yaw,0.000001f)&&a.e.sx==b.e.sx&&a.e.sy==b.e.sy&&a.e.sz==b.e.sz&&a.e.seed==b.e.seed,code,"runway/approach/PAPI fixture geometry changed");
  }
  for(float u=-g.length*.5f+3.f;u<=g.length*.5f-3.f;u+=40.f)
    for(float v:{-g.width*.5f+2.f,0.f,g.width*.5f-2.f}){
      vec3 p=frozenWorld(g,u,v);p.y=g_world.height(p.x,p.z,7)+1.f;
      check(g_scenery.collide(p,0.5f)==0,code,"collision object on runway contact corridor");
      check(g_scenery.obstacleTop(p.x,p.z,0.25f)<g.elev+0.1f,code,"obstacleTop intrudes into runway corridor");
    }
  entityCount+=localEntities;chunkCount+=localChunks;fixtureCount+=fixtures.size();
  printf("%s: %zu chunks, %zu entity footprints, %zu navigation fixtures, %s\n",code,localChunks,localEntities,fixtures.size(),failures==before?"clear/preserved":"FAILED");
  // Bound test memory across all sixteen independent destinations.
  g_scenery.clear();
}
} // namespace

int main(int argc,char** argv) {
  const char* path=argc>1?argv[1]:SOLACE_RUNWAY_GOLDEN;
  FILE* f=fopen(path,"rb");if(!f){printf("FAIL: cannot read immutable fixture %s\n",path);return 1;}
  uint64_t fingerprint=14695981039346656037ull;
  for(int ch;(ch=fgetc(f))!=EOF;)if(ch!='\r')fingerprint=(fingerprint^(uint8_t)ch)*1099511628211ull;
  if(fingerprint!=0xe7b9cff7394df91cull){fclose(f);puts("FAIL: immutable baseline fingerprint changed");return 1;}
  rewind(f);
  char magic[64],commit[64];int version=0;size_t count=0;
  if(fscanf(f,"%63s %d %63s %zu",magic,&version,commit,&count)!=4||strcmp(magic,"SOLACE_RUNWAY_GOLDEN")||version!=1||
      strcmp(commit,"52317bb1f74a5ee9e66e063e2f17d5be786955a0")||count!=16){fclose(f);puts("FAIL: invalid baseline identity");return 1;}
  std::vector<Golden> golden(count);for(auto& g:golden)if(!readGolden(f,g)){fclose(f);puts("FAIL: malformed baseline");return 1;}
  char trailing[2];bool eof=fscanf(f,"%1s",trailing)==EOF;fclose(f);check(eof,"fixture","unexpected trailing baseline data");
  testRunwayShader(path);
  g_world.build();check(!g_world.fromCache,"world","preservation test must generate current world, not load an old cache");
  testWorldTerrain(path);
  check(g_world.airports.size()==golden.size(),"world","airport/runway count changed");
  for(size_t ai=0;ai<golden.size()&&ai<g_world.airports.size();++ai){
    testDescriptors(golden[ai],g_world.airports[ai]);testTerrain(golden[ai],int(ai));testScenery(golden[ai]);
  }
  check(sampleCount==193625,"fixture","dense baseline sample coverage changed");
  printf("runway_preservation_test: %s, %zu terrain/contact samples, %zu navigation fixtures, %zu chunk visits, %zu entity footprints; max height delta %.9g m, max gradient delta %.9g (%d failures)\n",
    failures?"FAIL":"PASS",sampleCount,fixtureCount,chunkCount,entityCount,worstHeight,worstSlope,failures);
  return failures?1:0;
}
