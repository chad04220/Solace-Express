// Read-only nature submission audit. See docs/living-islands/performance/NATURE_SUBMISSIONS.md.
#include "entity_lod.h"
#include "entity_bounds.h"
#include <fstream>
#include <sstream>
#include <array>
#include <cstring>
const long oldV[13][3]={{1188,168,36},{1620,204,36},{4974,804,198},{2934,396,78},{3294,264,78},{990,264,108},{1248,120,60},{960,240,60},{960,240,60},{960,240,60},{1680,300,180},{1638,465,126},{2052,618,135}};
long newV[13][4]={}; // populated only from the current authored meshes
EntLocalBounds actualBounds[EK_COUNT],allBounds[EK_COUNT];
std::vector<EntChunkBoundsCache> chunkBounds(size_t(Scenery::NC)*Scenery::NC);
struct Shot{std::string name;vec3 cam,target;float fov=60;};
struct Result{long n[3][13][4]={};long hiddenHero=0,hiddenHeroTri=0;};
Result audit(const Shot&s,int q,int mode){
  Result out;auto R=entRangesFor(q),S=entBaseRangesFor(q);float farAll=std::max(R.big,R.build)+300,farDetail=std::max({R.tree,R.rock,R.bush})+300;
  int ccx=Scenery::chunkOf(s.cam.x),ccz=Scenery::chunkOf(s.cam.z),rad=int(ceilf(farAll/Scenery::CH))+1;
  mat4 vp=perspective(s.fov*DEG,1920.f/1080.f,.5f,90000.f)*lookAt(s.cam,s.target,{0,1,0});float pl[6][4];
  for(int i=0;i<3;++i)for(int k=0;k<2;++k)for(int c=0;c<4;++c)pl[i*2+k][c]=vp(3,c)+(k?-1.f:1.f)*vp(i,c);
  auto visible=[&](float x0,float y0,float z0,float x1,float y1,float z1){for(auto&P:pl){float x=P[0]>=0?x1:x0,y=P[1]>=0?y1:y0,z=P[2]>=0?z1:z0;if(P[0]*x+P[1]*y+P[2]*z+P[3]<0)return false;}return true;};
  float sunA=(14.3f-6.f)/12.f*PI;vec3 L=normalize(vec3(cosf(sunA),sinf(sunA)*.93f,.35f+.1f*sinf(sunA)));
  float shReach=sqrtf(std::max(1-L.y*L.y,0.f))/std::max(L.y,.15f),cR[2]={R.sh0,R.sh1};
  vec3 fwd=normalize(vec3(s.target.x-s.cam.x,0,s.target.z-s.cam.z)+vec3(1e-4f,0,0)),sc[2]={s.cam+fwd*(cR[0]*.45f),s.cam+fwd*(cR[1]*.45f)};
  for(int dz=-rad;dz<=rad;++dz)for(int dx=-rad;dx<=rad;++dx){
    int cx=ccx+dx,cz=ccz+dz;if(cx<0||cz<0||cx>=Scenery::NC||cz>=Scenery::NC)continue;
    float x0=Scenery::chunkX0(cx),z0=Scenery::chunkX0(cz),x1=x0+Scenery::CH,z1=z0+Scenery::CH;
    float ex=std::max({x0-s.cam.x,s.cam.x-x1,0.f}),ez=std::max({z0-s.cam.z,s.cam.z-z1,0.f});float horizontal=sqrtf(ex*ex+ez*ez);int level=horizontal<farDetail?2:horizontal<farAll?1:0;if(!level)continue;
    auto*ch=g_scenery.ensure(cx,cz,level);if(ch->ents.empty())continue;
    float ey=std::max({ch->ymin-s.cam.y,s.cam.y-ch->ymax,0.f}),dmin=sqrtf(ex*ex+ez*ez+ey*ey);
    float fx=std::max(fabsf(x0-s.cam.x),fabsf(x1-s.cam.x)),fz=std::max(fabsf(z0-s.cam.z),fabsf(z1-s.cam.z)),fy=std::max(fabsf(ch->ymin-s.cam.y),fabsf(ch->ymax-s.cam.y));float dmax=sqrtf(fx*fx+fz*fz+fy*fy);
    bool inView=dmin<farAll&&visible(x0-12,ch->ymin,z0-12,x1+12,ch->ymax,z1+12),inSh[2]={false,false};
    if(mode==4){auto&cached=chunkBounds[size_t(cz)*Scenery::NC+cx];const auto&b=cached.get(*ch,allBounds);inView=dmin<farAll&&cached.valid&&visible(b.lo.x,b.lo.y,b.lo.z,b.hi.x,b.hi.y,b.hi.z);}
    for(int c=0;c<2;++c){float r=cR[c]+std::max(210.f,ch->hmax*shReach+ch->reach+60),hx=std::max({x0-sc[c].x,sc[c].x-x1,0.f}),hz=std::max({z0-sc[c].z,sc[c].z-z1,0.f});inSh[c]=hx<r&&hz<r;}
    if(!inView&&!inSh[0]&&!inSh[1])continue;
    for(int k=0;k<=EK_SEASTACK;++k){uint32_t b=ch->off[k],e=ch->off[k+1];if(b==e)continue;float far=entRangeOf(R,k),l0,l1;entLodLimits(R,k,l0,l1);float close=mode>=2?entCloseLimit(R,k):0.f,sl0,sl1;entLodLimits(S,k,sl0,sl1);bool thin=entThins(k),viewK=inView&&dmin<far;if(!viewK&&!inSh[0]&&!inSh[1])continue;
      int lodN=entDetailAt(dmin,close,l0,l1),lodF=entDetailAt(dmax,close,l0,l1);bool bulk=viewK&&dmax<far&&lodN==lodF&&lodN!=3&&!entDetailSpanFades(k,dmin,dmax,close,l0,l1);
      if(bulk){uint32_t eb=e;if(thin){float kp=entKeepDrawn(k,std::max(dmin,1.f));eb=uint32_t(std::lower_bound(ch->ents.begin()+b,ch->ents.begin()+e,kp,[](const Ent&en,float v){return entThinKey(en)<v;})-ch->ents.begin());}out.n[0][k][lodN]+=eb-b;}
      if(bulk&&!inSh[0]&&!inSh[1])continue;
      for(uint32_t i=b;i<e;++i){auto&en=ch->ents[i];float d=length(vec3(en.x,en.y,en.z)-s.cam);int lod=entDetailAt(d,close,l0,l1);bool keep=!thin||entThinKey(en)<entKeepDrawn(k,d);
        if(viewK&&d<far&&!bulk&&keep&&(mode<3||lod!=3||entBoundsVisible(entInstanceBounds(actualBounds[k],k,en),pl))){++out.n[0][k][lod];int also=entDetailAlso(k,d,close,l0,l1);if(also>=0)++out.n[0][k][also];
          if(lod==3){auto I=kEntInfo[k];float rr=std::max(I.hx*en.sx,I.hz*en.sz)*1.4f+1.f;if(!visible(en.x-rr,en.y-2,en.z-rr,en.x+rr,en.y+I.h*en.sy*1.1f+1,en.z+rr)){++out.hiddenHero;out.hiddenHeroTri+=newV[k][3]/3;}}
        }
        bool shKeep=!thin||entThinKey(en)<entKeep(k,d);for(int c=0;c<2;++c)if(inSh[c]&&shKeep){float sx=en.x-sc[c].x,sz=en.z-sc[c].z,h=kEntInfo[k].h*en.sy,er=std::max(kEntInfo[k].hx*en.sx,kEntInfo[k].hz*en.sz)+h*shReach,cr=cR[c]*.78f+er;if(sx*sx+sz*sz>cr*cr)continue;int sl=c==0?std::min(entLodAt(d,sl0,sl1),1):2;if(c==1&&thin&&h<3)continue;++out.n[c+1][k][sl];}
      }
    }
  }return out;
}
#ifndef NATURE_SUBMISSION_LIBRARY
int main(int argc,char**argv){if(argc!=2){puts("Usage: nature_submission_probe PATH_TO_VERIFIED_WORLD_CACHE");return 2;}std::vector<EVert> vertices;EntMeshRange ranges[EK_COUNT];buildEntityMeshes(vertices,ranges);entBuildLocalBounds(vertices,ranges,actualBounds);entBuildLocalBounds(vertices,ranges,allBounds,true);for(int k=0;k<=EK_SEASTACK;++k)for(int l=0;l<4;++l)newV[k][l]=ranges[k].count[l];setvbuf(stdout,nullptr,_IONBF,0);const char*cache=argv[1];FILE*f=fopen(cache,"rb");if(!f)return 2;uint32_t magic;char stamp[65]={};fread(&magic,4,1,f);fread(stamp,1,64,f);fclose(f);World test;if(!test.loadCache(cache,stamp)){puts("cache verification failed; not rewriting it");return 3;}g_world.build(cache,stamp);
  std::vector<Shot> shots={{"foliage_closeup",{-23966,172.865112f,-4957.39014f},{-23998,162.865112f,-4999.39014f},53},{"forest_cedar_ridge",{-23860,217.061493f,-4790},{-24000,167.061493f,-5000},60},{"rock_closeup",{23043.5508f,262.832794f,10045.5195f},{23035.5508f,257.832794f,10035.5195f},53}};
  for(int j=0;j<4;++j){float a=j*PI*.5f;float x=-24000,z=-5000,y=g_world.height(x,z)+2;shots.push_back({"forest_ground_"+std::to_string(j),{x,y,z},{x+cosf(a)*50,y+3,z+sinf(a)*50},60});}
  puts("scene,q,mode,view_instances,view_triangles,near_shadow_triangles,far_shadow_triangles,close_instances,close_triangles,close_offscreen_instances,close_offscreen_triangles");
  for(int q:{1,2})for(auto&s:shots){if(q==2&&s.name!="foliage_closeup"&&s.name!="forest_ground_0")continue;for(int mode=0;mode<5;++mode){auto r=audit(s,q,mode);long n=0,tr[3]={},hero=0,heroTri=0;for(int p=0;p<3;++p)for(int k=0;k<=EK_SEASTACK;++k)for(int l=0;l<4;++l){if(mode==0&&l==3)continue;long nt=r.n[p][k][l],v=mode==0?oldV[k][l]:newV[k][l];tr[p]+=nt*v/3;if(p==0)n+=nt;if(p==0&&l==3){hero+=nt;heroTri+=nt*v/3;}}printf("%s,%d,%d,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld\n",s.name.c_str(),q,mode,n,tr[0],tr[1],tr[2],hero,heroTri,r.hiddenHero,r.hiddenHeroTri);if(mode==4&&s.name=="foliage_closeup")for(int k=0;k<=EK_SEASTACK;++k)fprintf(stderr,"DETAIL %s q%d %s counts %ld %ld %ld %ld\n",s.name.c_str(),q,kEntInfo[k].name,r.n[0][k][0],r.n[0][k][1],r.n[0][k][2],r.n[0][k][3]);}}
  return 0;
}
#endif
