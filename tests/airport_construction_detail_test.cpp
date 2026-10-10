// Focused close-asset construction contracts, separate from the atlas/bounds/wheel-ABI suite.
#include "entity_mesh.h"
#include <cmath>
#include <cstdio>
#include <set>
#include <tuple>
namespace {
int failures=0;
void require(bool value,const char* label,int kind){if(!value){++failures;std::printf("FAIL %s kind=%d\n",label,kind);}}
vec3 position(const EVert& e){return {e.px,e.py,e.pz};}
bool paintedSurfaceAhead(vec3 origin,vec3 direction,const EVert* e){
  const vec3 a=position(e[0]),u=position(e[1])-a,v=position(e[2])-a,h=cross(direction,v);
  const float determinant=dot(u,h);if(std::abs(determinant)<1e-10f)return false;
  const vec3 delta=origin-a;const float x=dot(delta,h)/determinant;if(x<0 || x>1)return false;
  const vec3 q=cross(delta,u);const float y=dot(direction,q)/determinant;if(y<0 || x+y>1)return false;
  const float distance=dot(v,q)/determinant;return distance>.00001f && distance<.025f;
}
}
int main(){
  std::vector<EVert> vertices;EntMeshRange ranges[EK_COUNT];buildEntityMeshes(vertices,ranges);
  auto each=[&](int kind,int lod,auto callback){const auto&r=ranges[kind];for(int i=r.first[lod];i<r.first[lod]+r.count[lod];++i)callback(vertices[i]);};
  // Every airport asset has an intentional close construction tier rather than an unreviewed alias.
  for(int kind=EK_HANGAR;kind<EK_COUNT;++kind)require(ranges[kind].count[3]>ranges[kind].count[0],"distinct hero construction",kind);
  for(int kind:{EK_CAR,EK_TRUCK,EK_GA_PLANE,EK_AIRLINER}){
    int curved=0;std::set<std::tuple<int,int,int>> directions;
    each(kind,3,[&](const EVert&e){if((e.part==P_PAINT || (kind==EK_TRUCK && e.part==P_METAL && e.pz<2.5f)) && std::abs(e.nx)>.08f && std::abs(e.ny)>.08f && std::abs(e.nz)>.04f){++curved;directions.emplace(int(e.nx*30),int(e.ny*30),int(e.nz*30));}});
    require(curved>100 && directions.size()>30,"multi-axis curved body surfaces",kind);
  }
  for(int lod:{0,1,3}){
    int panes=0,arch=0;
    each(EK_CAR,lod,[&](const EVert&e){if(e.part==P_GLASS && e.u>=1.f && e.u<=2.f && e.v>=0.f && e.v<=1.f)++panes;
      if(e.part==P_PAINT && std::abs(e.px)>.77f && e.py>.55f && e.py<.72f && (std::abs(e.pz-1.35f)<.3f || std::abs(e.pz+1.35f)<.3f))++arch;});
    require(panes>=48,"fitted pane UVs persist through road LODs",EK_CAR);
    require(arch>=12,"raised open-arch silhouette persists through road LODs",EK_CAR);
  }
  // Test the production mesh's tangent normals, not a duplicate interpolation implementation.
  const float stationZ[]={-2.185f,-2.08f,-1.85f,-1.45f,-1.f,-.55f,0,.55f,1.f,1.45f,1.85f,2.025f,2.12f};
  const float stationW[]={.66f,.77f,.835f,.86f,.838f,.83f,.831f,.841f,.852f,.86f,.827f,.775f,.73f};
  const float stationH[]={.77f,.80f,.83f,.895f,.916f,.918f,.92f,.92f,.894f,.867f,.816f,.793f,.777f};
  for(int lod:{0,1,3}){const auto&r=ranges[EK_CAR];int bodyEnd=r.first[lod];while(bodyEnd<r.first[lod]+r.count[lod] && vertices[bodyEnd].part==P_PAINT)++bodyEnd;
    bool bounded=true;for(int i=r.first[lod];i<bodyEnd;++i){const auto&e=vertices[i];int k=0;while(k<11 && e.pz>stationZ[k+1]+.000001f)++k;
      bounded&=std::abs(e.px)<=std::max(stationW[k],stationW[k+1])+.00002f;
      bounded&=e.py<=std::max(stationH[k],stationH[k+1])+.00002f && e.py>=.28498f;
      if(std::abs(e.px)<.00001f && e.py>.7f)bounded&=e.py>=std::min(stationH[k],stationH[k+1])-.00002f;}
    require(bounded,"monotone body loft has no station overshoot",EK_CAR);
    int joined=0;bool smooth=true;
    for(int k=1;k<12;++k){std::vector<float> left,right;float z=stationZ[k];
      for(int i=r.first[lod];i<bodyEnd;i+=3){const EVert*e=&vertices[i];float lo=std::min(e[0].pz,std::min(e[1].pz,e[2].pz)),hi=std::max(e[0].pz,std::max(e[1].pz,e[2].pz));
        bool before=lo<z-.00001f && std::abs(hi-z)<.00001f,after=hi>z+.00001f && std::abs(lo-z)<.00001f;if(!before&&!after)continue;
        for(int j=0;j<3;++j)if(std::abs(e[j].pz-z)<.00001f && std::abs(e[j].px)<.00001f && std::abs(e[j].py-stationH[k])<.00001f && e[j].ny>.5f)(before?left:right).push_back(-e[j].nz/e[j].ny);
      }
      if(left.empty() || right.empty())continue;++joined;
      float dl=(stationH[k]-stationH[k-1])/(stationZ[k]-stationZ[k-1]),dr=(stationH[k+1]-stationH[k])/(stationZ[k+1]-stationZ[k]);
      for(float a:left)for(float b:right){smooth&=std::abs(a-b)<.006f;if(dl*dr>0)smooth&=std::abs(a)>.45f*std::min(std::abs(dl),std::abs(dr));}
    }
    require(joined>=(lod==1?5:11) && smooth,"continuous nonflattened hood station tangents",EK_CAR);
  }

  // The curved nose collar must share the complete shell boundary, including T-junction subdivisions.
  // Exact edge IDs differ because the collar is denser; spatial coverage is checked in both directions.
  for(int lod:{0,1,3}){struct Segment{vec3 a,b;};std::vector<Segment> shell,collar;
    const auto&r=ranges[EK_CAR];for(int i=r.first[lod];i<r.first[lod]+r.count[lod];i+=3){const EVert* e=&vertices[i];if(e[0].part!=P_PAINT)continue;
      float lo=std::min(e[0].pz,std::min(e[1].pz,e[2].pz)),hi=std::max(e[0].pz,std::max(e[1].pz,e[2].pz));
      bool body=lo<2.119f && hi<2.12002f,bridge=lo>2.11998f && hi>2.1201f;if(!body&&!bridge)continue;
      for(int j=0;j<3;++j){const auto&a=e[j];const auto&b=e[(j+1)%3];if(std::abs(a.pz-2.12f)>.00002f || std::abs(b.pz-2.12f)>.00002f)continue;
        (body?shell:collar).push_back({{a.px,a.py,a.pz},{b.px,b.py,b.pz}});}}
    auto covered=[](const std::vector<Segment>&a,const std::vector<Segment>&b){for(const auto& edge:a)for(int j=0;j<=8;++j){vec3 p=edge.a+(edge.b-edge.a)*(float(j)/8);float best=1e9f;
      for(const auto& other:b){vec3 d=other.b-other.a;float t=clampf(dot(p-other.a,d)/std::max(dot(d,d),1e-12f),0,1);vec3 q=p-(other.a+d*t);best=std::min(best,dot(q,q));}
      if(best>2.5e-9f)return false;}return true;};
    require(shell.size()>=4 && collar.size()>=4 && covered(shell,collar) && covered(collar,shell),"closed shell-to-nose spatial join",EK_CAR);
  }
  for(int lod:{0,1,3}){std::vector<float> body,collar;const auto&r=ranges[EK_CAR];
    for(int i=r.first[lod];i<r.first[lod]+r.count[lod];i+=3){const EVert*e=&vertices[i];if(e[0].part!=P_PAINT)continue;float lo=std::min(e[0].pz,std::min(e[1].pz,e[2].pz)),hi=std::max(e[0].pz,std::max(e[1].pz,e[2].pz));
      bool before=lo<2.119f && hi<2.12002f,after=lo>2.11998f && hi>2.1201f;if(!before&&!after)continue;
      for(int j=0;j<3;++j)if(std::abs(e[j].px)<.00002f && std::abs(e[j].py-.777f)<.00002f && std::abs(e[j].pz-2.12f)<.00002f && e[j].ny>.5f)(before?body:collar).push_back(-e[j].nz/e[j].ny);}
    bool smooth=!body.empty()&&!collar.empty();for(float a:body)for(float b:collar)smooth&=std::abs(a-b)<.015f;
    require(smooth,"tangent-matched hood-to-fascia collar",EK_CAR);
  }
  // Forward rays through the actual lamp apertures must meet optics, not painted bumper triangles.
  int blocked=0;const auto& car=ranges[EK_CAR];
  for(int side:{-1,1})for(int sx=0;sx<16;++sx)for(int sy=0;sy<8;++sy){float x=(side<0?-.655f:.335f)+sx*(.320f/15),y=.520f+sy*(.125f/7);
    for(int i=car.first[3];i<car.first[3]+car.count[3];i+=3){const EVert*e=&vertices[i];if(e[0].part!=P_PAINT)continue;
      float ax=e[1].px-e[0].px,ay=e[1].py-e[0].py,bx=e[2].px-e[0].px,by=e[2].py-e[0].py,det=ax*by-ay*bx;if(std::abs(det)<1e-9f)continue;
      float u=((x-e[0].px)*by-(y-e[0].py)*bx)/det,v=(ax*(y-e[0].py)-ay*(x-e[0].px))/det;
      if(u>0 && v>0 && u+v<1 && e[0].pz+u*(e[1].pz-e[0].pz)+v*(e[2].pz-e[0].pz)>2.15f){++blocked;break;}}}
  require(blocked==0,"unobstructed lamp cavities",EK_CAR);
  int optics=0,reflectors=0;float lensMin=1e9f,lensMax=-1e9f;
  each(EK_CAR,3,[&](const EVert&e){if(e.part==P_GLASS && e.pz>2.f){++optics;lensMin=std::min(lensMin,e.pz);lensMax=std::max(lensMax,e.pz);}
    if(e.part==P_METAL && e.pz>2.f && (std::abs(e.nx)>.1f || std::abs(e.ny)>.1f))++reflectors;});
  require(optics>100 && lensMax-lensMin>.02f,"physical lamp lens depth",EK_CAR);
  require(reflectors>100,"angled reflector surfaces",EK_CAR);
  for(int kind:{EK_CAR,EK_TRUCK}){float forward=0;int bowl=0;
    each(kind,3,[&](const EVert&e){if(e.part!=P_METAL)return;
      bool cup=kind==EK_CAR ? e.pz>2.11f && e.pz<2.154f && std::abs(e.px)>.345f && e.py>.525f && e.py<.65f : e.pz>4.505f && e.pz<4.536f && std::abs(e.px)>.73f && e.py>.79f && e.py<.97f;
      if(cup && (std::abs(e.nx)>.2f || std::abs(e.ny)>.2f)){forward+=e.nz;++bowl;}});
    require(bowl>100 && forward>0,"concave lamp optics face outward",kind);
  }
  for(int kind:{EK_CAR,EK_TRUCK})for(int slot=0;slot<(kind==EK_CAR?4:6);++slot){
    std::set<int> axial;int sidewall=0,spoke=0;
    each(kind,3,[&](const EVert&e){if(e.part!=P_WHEEL0+slot)return;axial.insert(int(std::round(std::abs(e.px)*10000)));
      float radial=std::hypot(e.py-e.u,e.pz-e.v)/e.ao;
      if(radial>.75f && std::abs(e.nx)>.25f)++sidewall;
      if(radial<.55f && std::abs(e.nx)>.7f)++spoke;});
    require(axial.size()>=8 && sidewall>60 && spoke>60,"rounded tire and recessed physical rim",kind);
  }
  for(int kind:{EK_HANGAR,EK_TERMINAL,EK_CTRL_TOWER,EK_FBO,EK_JETBRIDGE}){int panes=0;
    each(kind,3,[&](const EVert&e){if(e.part==P_GLASS && e.u>=1.f && e.u<=2.f && e.v>=0 && e.v<=1)++panes;});
    require(panes>=96,"fitted airport glazing",kind);
  }
  for(int lod:{0,1,3})for(int end=0;end<2;++end){float z=end?1.65f+.75f*cosf(.0181828f):-3.5f-.8f*cosf(.0181828f);int centre=0;
    each(EK_TRUCK,lod,[&](const EVert&e){if(e.part==P_METAL && std::abs(e.px)<.000001f && std::abs(e.py-1.95f)<.000001f && std::abs(e.pz-z)<.000001f && (end?e.nz:-e.nz)>.999f)++centre;});
    require(centre>=(lod==3?32:lod==0?12:8),"outward sealed tank-head poles",EK_TRUCK);
  }
  // Large hangar doors must not conceal a still-solid end wall: logarithmic depth exposes that overlap.
  for(int lod=0;lod<ENT_LODS;++lod){int obstructed=0;const auto&r=ranges[EK_ARCH_HANGAR];
    for(int x=0;x<13;++x)for(int y=0;y<9;++y){float px=-4.9f+x*(9.8f/12),py=.2f+y*(5.3f/8);
      for(int i=r.first[lod];i<r.first[lod]+r.count[lod];i+=3){const EVert*e=&vertices[i];if(e[0].part!=P_WALL || e[0].pz<11.9f || e[1].pz<11.9f || e[2].pz<11.9f)continue;
        float ax=e[1].px-e[0].px,ay=e[1].py-e[0].py,bx=e[2].px-e[0].px,by=e[2].py-e[0].py,det=ax*by-ay*bx;if(std::abs(det)<1e-9f)continue;
        float u=((px-e[0].px)*by-(py-e[0].py)*bx)/det,v=(ax*(py-e[0].py)-ay*(px-e[0].px))/det;if(u>=0 && v>=0 && u+v<=1){++obstructed;break;}}
    }
    require(obstructed==0,"arch hangar door has a real unobstructed wall opening",EK_ARCH_HANGAR);
  }
  // A fitted dispenser display must remain the visible surface, not hide behind the coarse sign.
  {const auto&r=ranges[EK_FUEL_PUMP];int exposed=0,concrete=0;bool metalCabinet=true;
    for(int i=r.first[3];i<r.first[3]+r.count[3];++i){const auto&e=vertices[i];
      if(e.part==P_TRIM){if(e.pz>=1.f && e.py>.12f)metalCabinet=false;else ++concrete;}}
    for(int x=0;x<9;++x)for(int y=0;y<5;++y){float px=-.25f+x*.0625f,py=1.18f+y*.05f,closest=-1e9f;int part=-1;
      for(int i=r.first[3];i<r.first[3]+r.count[3];i+=3){const EVert*e=&vertices[i];
        float ax=e[1].px-e[0].px,ay=e[1].py-e[0].py,bx=e[2].px-e[0].px,by=e[2].py-e[0].py,det=ax*by-ay*bx;if(std::abs(det)<1e-9f)continue;
        float u=((px-e[0].px)*by-(py-e[0].py)*bx)/det,v=(ax*(py-e[0].py)-ay*(px-e[0].px))/det;
        if(u>=-.000001f && v>=-.000001f && u+v<=1.000001f){float z=e[0].pz+u*(e[1].pz-e[0].pz)+v*(e[2].pz-e[0].pz);if(z>closest){closest=z;part=e[0].part;}}}
      if(part==P_GLASS && std::abs(closest-1.475f)<.00002f)++exposed;
    }
    require(exposed==45,"fuel dispenser display is exposed across its face",EK_FUEL_PUMP);
    require(metalCabinet && concrete>0,"metal dispenser casing retains concrete pad and piers",EK_FUEL_PUMP);
  }
  // Check actual glass triangles against actual fuselage triangles, including near-edge samples.
  // A differently tessellated overlay can penetrate the body even when every vertex is outside it.
  {const auto&r=ranges[EK_GA_PLANE];int samples=0,obstructed=0;
    const float weights[][3]={{1.f/3,1.f/3,1.f/3},{.8f,.1f,.1f},{.1f,.8f,.1f},{.1f,.1f,.8f}};
    for(int i=r.first[3];i<r.first[3]+r.count[3];i+=3){const EVert*e=&vertices[i];if(e[0].part!=P_GLASS)continue;
      for(const auto&weight:weights){vec3 p(0),normal(0);for(int j=0;j<3;++j){p=p+position(e[j])*weight[j];normal=normal+vec3(e[j].nx,e[j].ny,e[j].nz)*weight[j];}normal=normalize(normal);++samples;
        for(int j=r.first[3];j<r.first[3]+r.count[3];j+=3)if(vertices[j].part==P_PAINT && paintedSurfaceAhead(p,normal,&vertices[j])){++obstructed;break;}}}
    require(samples==1960 && obstructed==0,"GA fitted panes clear painted fuselage including edges",EK_GA_PLANE);
  }
  // The expensive close tier must remain independently budgeted; traffic outside its range uses the lofted low tiers.
  require(ranges[EK_CAR].count[0]<=3600 && ranges[EK_CAR].count[1]<=2400 && ranges[EK_CAR].count[3]<=27000,"bounded sedan construction work",EK_CAR);
  require(ranges[EK_TRUCK].count[3]<=25000 && ranges[EK_AIRLINER].count[3]<=64000,"bounded airport hero work",EK_AIRLINER);
  std::printf("airport construction detail: %s (%d failures)\n",failures?"FAIL":"PASS",failures);return failures?1:0;
}
