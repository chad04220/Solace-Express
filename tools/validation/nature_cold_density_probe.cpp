// Deterministic, read-only cold-forest stress sample; see NATURE_SUBMISSIONS.md.
#include "scenery.h"
#define NATURE_SUBMISSION_LIBRARY
#include "nature_submission_probe.cpp"
int main(int argc,char**argv) {
 if(argc!=2)return 2; const char*path=argv[1];FILE*f=fopen(path,"rb");if(!f)return 2;uint32_t magic;char stamp[65]={};fread(&magic,4,1,f);fread(stamp,1,64,f);fclose(f);World test;if(!test.loadCache(path,stamp))return 3;g_world.build(path,stamp);
 std::vector<EVert> vertices;EntMeshRange ranges[EK_COUNT];buildEntityMeshes(vertices,ranges);entBuildLocalBounds(vertices,ranges,actualBounds);entBuildLocalBounds(vertices,ranges,allBounds,true);for(int k=0;k<=EK_SEASTACK;++k)for(int l=0;l<4;++l)newV[k][l]=ranges[k].count[l];
 struct Candidate {float score,x,z;};std::vector<Candidate> candidates;
 for(float z=-39000;z<39000;z+=512)for(float x=-39000;x<39000;x+=512){float b[4],m[4];g_world.sampleBase(x,z,b);float y=g_world.height(x,z);if((b[3]<=.45f&&y<=650)||b[1]<8||y<9)continue;g_world.sampleMask(x,z,m);float f=g_world.forestAt(x,z),tl=smoothstepf(1500-b[3]*900,1100-b[3]*700,y);float fd=smoothstepf(.42f-.1f*b[2],.5f-.1f*b[2],f)*tl*smoothstepf(9,18,m[0]*ROAD_RANGE)*(1-smoothstepf(.03f,.2f,m[1]))*(1-.9f*m[3]);if(fd>.8f)candidates.push_back({fd+.001f*f,x,z});}
 std::sort(candidates.begin(),candidates.end(),[](auto&a,auto&b){return a.score>b.score;});long best=0;vec3 cam;int checked=0,bestSpecies[7]={};std::vector<vec2> sites;
 for(auto&c:candidates){bool close=false;for(auto&p:sites)if(hypotf(p.x-c.x,p.y-c.z)<1024)close=true;if(close)continue;sites.push_back({c.x,c.z});int counts[7]={};long cost=0;vec3 here(c.x,g_world.height(c.x,c.z)+2,c.z);int cx=Scenery::chunkOf(c.x),cz=Scenery::chunkOf(c.z);
  for(int dz=-1;dz<=1;++dz)for(int dx=-1;dx<=1;++dx){auto*ch=g_scenery.ensure(cx+dx,cz+dz,2);if(!ch)continue;for(int k=0;k<=EK_BUSH;++k)for(uint32_t i=ch->off[k];i<ch->off[k+1];++i){const auto&e=ch->ents[i];if(length(vec3(e.x,e.y,e.z)-here)<entCloseLimit(entRangesFor(1),k)){++counts[k];cost+=newV[k][3]/3;}}}
  if(cost>best){best=cost;cam=here;std::copy(counts,counts+7,bestSpecies);}if(++checked==32)break;
 }
 fprintf(stderr,"cold_candidates=%zu sampled_sites=%d best_camera=%.6f,%.6f,%.6f omnidirectional_close_triangles=%ld\n",candidates.size(),checked,cam.x,cam.y,cam.z,best);for(int k=0;k<=EK_BUSH;++k)fprintf(stderr,"close_species %s %d\n",kEntInfo[k].name,bestSpecies[k]);
 puts("scene,q,mode,view_triangles,close_instances,close_triangles,near_shadow_triangles,far_shadow_triangles");
 for(int q:{1,2})for(int heading=0;heading<4;++heading){float a=heading*PI*.5f;Shot s{"cold_forest_"+std::to_string(heading),cam,cam+vec3(cosf(a)*50,3,sinf(a)*50),60};for(int mode:{0,2,4}){auto r=audit(s,q,mode);long tr=0,hero=0,heroTri=0,ns=0,fs=0;for(int p=0;p<3;++p)for(int k=0;k<=EK_SEASTACK;++k)for(int l=0;l<(mode?4:3);++l){long n=r.n[p][k][l],v=mode?newV[k][l]:oldV[k][l];if(p==0)tr+=n*v/3;else if(p==1)ns+=n*v/3;else fs+=n*v/3;if(p==0&&l==3){hero+=n;heroTri+=n*v/3;}}printf("%s,%d,%d,%ld,%ld,%ld,%ld,%ld\n",s.name.c_str(),q,mode,tr,hero,heroTri,ns,fs);}}
}
