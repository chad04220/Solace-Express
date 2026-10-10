// Read-only production-placement submission audit. No OpenGL or FPS estimate.
#include "entity_lod.h"
#include "entity_mesh.h"
#include "scenery.h"
#ifndef LEGACY
#include "entity_bounds.h"
#if __has_include("near_shadow_coverage.h")
#include "near_shadow_coverage.h"
#define HAS_ADAPTIVE_SHADOW_COVERAGE 1
#endif
EntLocalBounds actualBounds[EK_COUNT],allBounds[EK_COUNT];
std::vector<EntChunkBoundsCache> chunkBounds(size_t(Scenery::NC)*Scenery::NC);
#else
inline int entDetailAt(float d,float,float l0,float l1){return entLodAt(d,l0,l1);}
inline int entDetailAlso(int k,float d,float,float l0,float l1){return entLodFades(k)?entLodAlso(d,l0,l1):-1;}
inline bool entDetailSpanFades(int k,float a,float b,float,float l0,float l1){return entLodFades(k)&&entLodSpanFades(a,b,l0,l1);}
#endif
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cstring>
long meshV[EK_COUNT][4]={};
struct Shot{std::string name;vec3 cam,target;float fov=60;};
struct Result{long n[3][EK_COUNT][4]={};};
Result audit(const Shot&s,int q,int mode,float nearRadius=0.f){
  Result out;auto R=entRangesFor(q),S=entBaseRangesFor(q);float farAll=std::max(R.big,R.build)+300,farDetail=std::max({R.tree,R.rock,R.bush})+300;
  int ccx=Scenery::chunkOf(s.cam.x),ccz=Scenery::chunkOf(s.cam.z),rad=int(ceilf(farAll/Scenery::CH))+1;
  mat4 vp=perspective(s.fov*DEG,1920.f/1080.f,.5f,90000.f)*lookAt(s.cam,s.target,{0,1,0});float pl[6][4];
  for(int i=0;i<3;++i)for(int k=0;k<2;++k)for(int c=0;c<4;++c)pl[i*2+k][c]=vp(3,c)+(k?-1.f:1.f)*vp(i,c);
  auto visible=[&](float x0,float y0,float z0,float x1,float y1,float z1){for(auto&P:pl){float x=P[0]>=0?x1:x0,y=P[1]>=0?y1:y0,z=P[2]>=0?z1:z0;if(P[0]*x+P[1]*y+P[2]*z+P[3]<0)return false;}return true;};
  float sunA=(14.3f-6.f)/12.f*PI;vec3 L=normalize(vec3(cosf(sunA),sinf(sunA)*.93f,.35f+.1f*sinf(sunA)));
  float shReach=sqrtf(std::max(1-L.y*L.y,0.f))/std::max(L.y,.15f),cR[2]={nearRadius>0.f?nearRadius:R.sh0,R.sh1};
  vec3 fwd=normalize(vec3(s.target.x-s.cam.x,0,s.target.z-s.cam.z)+vec3(1e-4f,0,0)),sc[2]={s.cam+fwd*(cR[0]*.45f),s.cam+fwd*(cR[1]*.45f)};
  for(int dz=-rad;dz<=rad;++dz)for(int dx=-rad;dx<=rad;++dx){
    int cx=ccx+dx,cz=ccz+dz;if(cx<0||cz<0||cx>=Scenery::NC||cz>=Scenery::NC)continue;
    float x0=Scenery::chunkX0(cx),z0=Scenery::chunkX0(cz),x1=x0+Scenery::CH,z1=z0+Scenery::CH;
    float ex=std::max({x0-s.cam.x,s.cam.x-x1,0.f}),ez=std::max({z0-s.cam.z,s.cam.z-z1,0.f});float horizontal=sqrtf(ex*ex+ez*ez);int level=horizontal<farDetail?2:horizontal<farAll?1:0;if(!level)continue;
    auto*ch=g_scenery.ensure(cx,cz,level);if(ch->ents.empty())continue;
    float ey=std::max({ch->ymin-s.cam.y,s.cam.y-ch->ymax,0.f}),dmin=sqrtf(ex*ex+ez*ez+ey*ey);
    float fx=std::max(fabsf(x0-s.cam.x),fabsf(x1-s.cam.x)),fz=std::max(fabsf(z0-s.cam.z),fabsf(z1-s.cam.z)),fy=std::max(fabsf(ch->ymin-s.cam.y),fabsf(ch->ymax-s.cam.y));float dmax=sqrtf(fx*fx+fz*fz+fy*fy);
    bool inView=dmin<farAll&&visible(x0-12,ch->ymin,z0-12,x1+12,ch->ymax,z1+12),inSh[2]={false,false};
#ifndef LEGACY
    if(mode==3||mode==4){auto&cached=chunkBounds[size_t(cz)*Scenery::NC+cx];const auto&b=cached.get(*ch,allBounds);inView=dmin<farAll&&cached.valid&&visible(b.lo.x,b.lo.y,b.lo.z,b.hi.x,b.hi.y,b.hi.z);}
#endif
    for(int c=0;c<2;++c){float r=cR[c]+std::max(210.f,ch->hmax*shReach+ch->reach+60),hx=std::max({x0-sc[c].x,sc[c].x-x1,0.f}),hz=std::max({z0-sc[c].z,sc[c].z-z1,0.f});inSh[c]=hx<r&&hz<r;}
    if(!inView&&!inSh[0]&&!inSh[1])continue;
    for(int k=0;k<EK_COUNT;++k){uint32_t b=ch->off[k],e=ch->off[k+1];if(b==e)continue;float far=entRangeOf(R,k),l0,l1;entLodLimits(R,k,l0,l1);float close=0.f,sl0,sl1;
#ifndef LEGACY
      if(mode>=1&&mode<4)close=entCloseLimit(R,k);
#endif
      entLodLimits(S,k,sl0,sl1);bool thin=entThins(k),viewK=inView&&dmin<far;if(!viewK&&!inSh[0]&&!inSh[1])continue;
      int lodN=entDetailAt(dmin,close,l0,l1),lodF=entDetailAt(dmax,close,l0,l1);bool bulk=viewK&&dmax<far&&lodN==lodF&&lodN!=3&&!entDetailSpanFades(k,dmin,dmax,close,l0,l1);
      if(bulk){uint32_t eb=e;if(thin){float kp=entKeepDrawn(k,std::max(dmin,1.f));eb=uint32_t(std::lower_bound(ch->ents.begin()+b,ch->ents.begin()+e,kp,[](const Ent&en,float v){return entThinKey(en)<v;})-ch->ents.begin());}out.n[0][k][lodN]+=eb-b;}
      if(bulk&&!inSh[0]&&!inSh[1])continue;
      for(uint32_t i=b;i<e;++i){auto&en=ch->ents[i];float d=length(vec3(en.x,en.y,en.z)-s.cam);int lod=entDetailAt(d,close,l0,l1);bool keep=!thin||entThinKey(en)<entKeepDrawn(k,d),closeVisible=true;
#ifndef LEGACY
        closeVisible=mode<2||lod!=3||!actualBounds[k].valid||entBoundsVisible(entInstanceBounds(actualBounds[k],k,en),pl);
#endif
        if(viewK&&d<far&&!bulk&&keep&&closeVisible){++out.n[0][k][lod];int also=entDetailAlso(k,d,close,l0,l1);if(also>=0&&meshV[k][also]>0)++out.n[0][k][also];}
        bool shKeep=!thin||entThinKey(en)<entKeep(k,d);for(int c=0;c<2;++c)if(inSh[c]&&shKeep&&k!=EK_RWYLIGHT&&k!=EK_PAPI){float sx=en.x-sc[c].x,sz=en.z-sc[c].z,h=kEntInfo[k].h*en.sy,er=std::max(kEntInfo[k].hx*en.sx,kEntInfo[k].hz*en.sz)+h*shReach,cr=cR[c]*.78f+er;if(sx*sx+sz*sz>cr*cr)continue;int sl=c==0?std::min(entLodAt(d,sl0,sl1),1):(entClass(k)==EC_BUILDING?1:2);if(c==1&&thin&&h<3)continue;++out.n[c+1][k][sl];}
      }
    }
  }return out;
}
std::vector<Shot> makeShots(){
  std::vector<Shot>v;
  for(auto town:std::vector<std::pair<std::string,vec3>>{{"capital",{-3200,0,-1200}},{"port_verde",{-29500,0,10500}}}){
    auto p=town.second;p.y=g_world.height(p.x,p.z);
    v.push_back({town.first+"_overview",p+vec3(430,210,560),p+vec3(0,20,0),60});
    v.push_back({town.first+"_street",p+vec3(18,2.5f,18),p+vec3(-120,9,-120),60});
    float best=1e30f;Ent chosen{};bool found=false;
    for(int dz=-5;dz<=5;++dz)for(int dx=-5;dx<=5;++dx){auto*c=g_scenery.ensure(Scenery::chunkOf(p.x)+dx,Scenery::chunkOf(p.z)+dz,2);if(!c)continue;for(int k:{EK_TOWER,EK_SKYSCRAPER})for(unsigned i=c->off[k];i<c->off[k+1];++i){const auto&e=c->ents[i];float d=(e.x-p.x)*(e.x-p.x)+(e.z-p.z)*(e.z-p.z);if(d<best){best=d;chosen=e;found=true;}}}
    if(found){vec3 t(chosen.x,chosen.y+24,chosen.z);v.push_back({town.first+"_tower_close",t+vec3(70,-18,95),t,60});}
  }
  int ap=g_world.findAirport("CAP");if(ap>=0){auto&a=g_world.airports[ap];vec3 t=a.pos()+vec3(200,15,0);v.push_back({"airport_capital",t+vec3(500,175,550),t,60});std::vector<AptItem>items;airportItems(ap,items);for(auto&i:items)if(i.kind==EK_TRUCK){vec3 t(i.e.x,i.e.y+1.6f,i.e.z),local(11,7,17);float a=i.e.yaw;vec3 off(cosf(a)*local.x+sinf(a)*local.z,local.y,-sinf(a)*local.x+cosf(a)*local.z);v.push_back({"airport_ramp_close",t+off,t,57});break;}}
  return v;
}
int main(int argc,char**argv){if(argc!=4&&argc!=5){puts("Usage: probe VERIFIED_WORLD_CACHE FROZEN_VIEWS.csv WRITE_VIEWS_0_OR_1 [settled]");return 2;}const bool settled=argc==5;if(settled&&strcmp(argv[4],"settled")){fprintf(stderr,"unsupported shadow policy\n");return 2;}
#ifndef HAS_ADAPTIVE_SHADOW_COVERAGE
 if(settled){fprintf(stderr,"this source snapshot has no adaptive controller\n");return 2;}
#endif
 setvbuf(stdout,nullptr,_IONBF,0);FILE*f=fopen(argv[1],"rb");if(!f)return 2;uint32_t magic;char stamp[65]={};fread(&magic,4,1,f);fread(stamp,1,64,f);fclose(f);World test;if(!test.loadCache(argv[1],stamp)){fprintf(stderr,"cache rejected, refusing regeneration\n");return 3;}g_world.build(argv[1],stamp);
 std::vector<EVert>vertices;EntMeshRange ranges[EK_COUNT];buildEntityMeshes(vertices,ranges);for(int k=0;k<EK_COUNT;++k)for(int l=0;l<ENT_LODS;++l)meshV[k][l]=ranges[k].count[l];
#ifndef LEGACY
 entBuildLocalBounds(vertices,ranges,actualBounds);entBuildLocalBounds(vertices,ranges,allBounds,true);
#endif
 std::vector<Shot>shots;if(atoi(argv[3])){shots=makeShots();std::ofstream o(argv[2]);o<<std::setprecision(9);for(auto&s:shots)o<<s.name<<' '<<s.cam.x<<' '<<s.cam.y<<' '<<s.cam.z<<' '<<s.target.x<<' '<<s.target.y<<' '<<s.target.z<<' '<<s.fov<<'\n';}else{std::ifstream in(argv[2]);Shot s;while(in>>s.name>>s.cam.x>>s.cam.y>>s.cam.z>>s.target.x>>s.target.y>>s.target.z>>s.fov)shots.push_back(s);}
 printf("scene,q,mode,group,view_instances,view_triangles,view_draws,near_shadow_instances,near_shadow_triangles,near_shadow_draws,far_shadow_instances,far_shadow_triangles,far_shadow_draws,close_instances,close_triangles");
 if(settled)printf(",near_radius,fade_inner,fade_outer,settled_agl");putchar('\n');
 for(int q:{1,2})for(auto&s:shots)for(int mode=0;mode<5;++mode){
#ifdef LEGACY
 if(mode)continue;
#endif
 if(settled&&mode!=3)continue;
 float nearRadius=0.f,fadeInner=0.f,fadeOuter=0.f,agl=0.f;
#ifdef HAS_ADAPTIVE_SHADOW_COVERAGE
 if(settled){NearShadowCoverage coverage;for(int frame=0;frame<600;++frame)coverage.update(s.cam,1.f/60.f,entBaseRangesFor(q).sh0,true,false,[](float x,float z){return g_world.groundHeight(x,z);});if(coverage.phase()!=NearShadowCoverage::Phase::Stable||coverage.fadeScale()!=1.f||coverage.speed()!=0.f){fprintf(stderr,"controller did not settle\n");return 4;}nearRadius=coverage.radius();auto fade=coverage.fadeRadii(nearRadius);fadeInner=fade.x;fadeOuter=fade.y;agl=coverage.agl();}
#endif
 auto r=audit(s,q,mode,nearRadius);for(int group=0;group<4;++group){long n[3]={},tri[3]={},draws[3]={},hero=0,heroTri=0;for(int k=0;k<EK_COUNT;++k){bool inc=group==0||(group==1&&k<=EK_SEASTACK)||(group==2&&k>EK_SEASTACK)||(group==3&&(k==EK_TOWER||k==EK_SKYSCRAPER));if(!inc)continue;for(int p=0;p<3;++p)for(int l=0;l<4;++l){long nt=r.n[p][k][l];if(!meshV[k][l])continue;n[p]+=nt;tri[p]+=nt*meshV[k][l]/3;draws[p]+=nt>0;if(p==0&&l==3){hero+=nt;heroTri+=nt*meshV[k][l]/3;}}}printf("%s,%d,%d,%s,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld",s.name.c_str(),q,mode,(const char*[]){"all","nature","structures","tower_sky"}[group],n[0],tri[0],draws[0],n[1],tri[1],draws[1],n[2],tri[2],draws[2],hero,heroTri);if(settled)printf(",%.9g,%.9g,%.9g,%.9g",nearRadius,fadeInner,fadeOuter,agl);putchar('\n');}}
}
