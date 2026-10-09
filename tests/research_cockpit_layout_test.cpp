// CPU-only contract for the actual GLSL research layout. No OpenGL context, cameras or mesh bake.
// Compile: c++ -std=c++17 -O2 tests/research_cockpit_layout_test.cpp -o research_cockpit_layout_test
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>
#include <vector>
using std::max;
using std::min;
static float max(float a,double b){return std::max(a,float(b));}
struct vec2 { float x,y; explicit vec2(float v):x(v),y(v){} vec2(float a,float b):x(a),y(b){} vec2():x(0),y(0){} };
struct vec3 { float x,y,z; explicit vec3(float v):x(v),y(v),z(v){} vec3(float a,float b,float c):x(a),y(b),z(c){} vec3():x(0),y(0),z(0){} };
static vec2 operator+(vec2 a,vec2 b){return vec2(a.x+b.x,a.y+b.y);}
static vec2 operator-(vec2 a,vec2 b){return vec2(a.x-b.x,a.y-b.y);}
static vec3 operator+(vec3 a,vec3 b){return vec3(a.x+b.x,a.y+b.y,a.z+b.z);}
static vec3 operator-(vec3 a,vec3 b){return vec3(a.x-b.x,a.y-b.y,a.z-b.z);}
static vec3 operator-(vec3 a){return vec3(-a.x,-a.y,-a.z);}
static vec3 operator*(vec3 a,float b){return vec3(a.x*b,a.y*b,a.z*b);}
static vec3 operator/(vec3 a,float b){return a*(1.0f/b);}
static vec2 abs(vec2 a){return vec2(std::abs(a.x),std::abs(a.y));}
static vec3 abs(vec3 a){return vec3(std::abs(a.x),std::abs(a.y),std::abs(a.z));}
static float dot(vec3 a,vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
static vec3 cross(vec3 a,vec3 b){return vec3(a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x);}
static float length(vec3 a){return std::sqrt(dot(a,a));}
static vec3 normalize(vec3 a){return a/length(a);}
static float atan(float a,float b){return std::atan2(a,b);}
// The production file is compiled as C++ too, keeping panel definitions and frame math authoritative.
#include "../src/shaders/research_cockpit_layout.glsl"
static int failures=0, checks=0;
static void check(bool pass,const char* name){++checks;if(!pass){++failures;if(failures<15)std::printf("FAIL: %s\n",name);}}
static vec3 point(ResearchPanel p,vec3 l){vec3 t=normalize(cross(vec3(0,1,0),p.n));return p.c+t*l.x+cross(p.n,t)*l.y+p.n*l.z;}
static float box(vec3 p,vec3 h){vec3 d=abs(p)-h;return length(vec3(max(d.x,0.f),max(d.y,0.f),max(d.z,0.f)))+min(max(d.x,max(d.y,d.z)),0.f);}
static float footwellLight(vec3 q){
  q.x=std::abs(q.x);vec3 a(.286f,-.83f,-.77f),ab(0,0,.29f),p=q-a;
  return length(p-ab*std::clamp(dot(p,ab)/dot(ab,ab),0.f,1.f))-.008f;
}
static float panelBody(vec3 q,ResearchPanel p,bool wr){
  vec3 l=researchPanelFrame(q,p);float border=wr?.020f:.018f,depth=wr?.028f:.030f;
  float d=box(l+vec3(0,0,depth),vec3(p.h.x+border-.012f,p.h.y+border-.012f,depth-.012f))-.012f;
  return max(d,(std::abs(l.x)+std::abs(l.y)-p.h.x-p.h.y-2*border+p.corner)*.70710678f);
}
static bool specterWindowCut(vec3 q){
  float r=std::hypot(q.x,q.z);
  if(r>.626f){vec3 p=q*(.634f/r);if(std::abs(atan(p.x,-p.z))<1.25f&&std::abs(p.y-.02f)<.30f)return true;}
  if(std::abs(q.x)>.622f){vec3 p=q*(.63f/std::abs(q.x));if(std::abs(p.y-.04f)<.2f&&std::abs(p.z-.24f)<.3f)return true;}
  return false;
}
static bool wraithFrontCut(vec3 q){
  // The unchanged panoramic window's cylindrical analytic cut.
  vec2 d(q.x,q.z+.15f);float r=std::hypot(d.x,d.y);if(1.f-r>.023f)return false;
  float qa=q.x*q.x+q.z*q.z,qb=-q.z*.15f;
  for(float radius:{.981f,.996f}){
    float qc=.15f*.15f-radius*radius,t=(qb+std::sqrt(max(qb*qb-qa*qc,0.f)))/max(qa,1e-8f);
    vec3 p=q*(radius==.981f?min(t,1.f):t);float arc=atan(p.x,-(p.z+.15f));
    if(max(max(std::abs(arc)-.84f,std::abs(p.y-.07f)-.33f),std::abs(p.x)+p.y+.18f-1.2f)>=0.f)return false;
  }
  return true;
}
static bool wraithChinCut(vec3 q){
  ResearchPanel pane;pane.c=vec3(0,-.5f,-1);pane.n=vec3(0,.7509f,.6604f);pane.h=vec2(.38f,.2f);pane.corner=.07f;
  vec3 l=researchPanelFrame(q,pane);if(l.z>.023f)return false;
  float qn=dot(q,pane.n),cn=dot(pane.c,pane.n);if(qn>-1e-5f)return false;
  for(float h:{.019f,.004f}){vec3 p=q*(h==.019f?min((h+cn)/qn,1.f):(h+cn)/qn);vec3 a=researchPanelFrame(p,pane);if(researchPanelShape(vec2(a.x,a.y),pane)>=0.f)return false;}
  return true;
}
static float wraithBridge(vec3 q){
  vec3 l=researchPanelFrame(q,wraithPanel(1));
  return box(l-WR_BRIDGE_C,WR_BRIDGE_HALF-vec3(WR_BRIDGE_ROUND))-WR_BRIDGE_ROUND;
}
static float structure(vec3 q,bool wr){
  float d=1e6f;for(int i=0;i<(wr?3:5);++i)d=min(d,panelBody(q,wr?wraithPanel(i):specterPanel(i),wr));
  if(!wr){float r=std::hypot(q.x,q.z),a=atan(q.x,-q.z);float saddle=max(max(r-.71f,.47f-r),max(std::abs(a)-1.18f,max(q.y+.505f,-.56f-q.y)));saddle=max(saddle,.47f+std::clamp(-.54f-q.y,0.f,.02f)-r);return min(d,min(saddle,footwellLight(q)));}
  ResearchPanel p=wraithPanel(1);vec3 l=researchPanelFrame(q,p);
  float bridge=wraithBridge(q);
  float rail=box(l-vec3(0,-.155f,-.019f),vec3(.282f,.009f,.019f))-.008f;
  return min(d,min(bridge,rail));
}
static vec3 rotateStick(vec3 l,float pitch,float roll){
  // transpose(partRxy(roll*.25)*partRyz(-pitch*.25)), exactly the production pose.
  float ar=roll*.25f,ap=-pitch*.25f,cr=std::cos(ar),sr=std::sin(ar),cp=std::cos(ap),sp=std::sin(ap);
  vec3 r(cr*l.x+sr*l.y,-sr*l.x+cr*l.y,l.z);
  return vec3(r.x,cp*r.y+sp*r.z,-sp*r.y+cp*r.z);
}
int main(){
  float minFaceGap=1e6f,minControlGap=1e6f;
  // Exact physical shin-surface witnesses from the explicit seated proxy at full rudder.
  // The former square lower rim put each point 2.38 mm inside the saddle.
  // These fixed regression points avoid importing a body-fitting search into the layout contract.
  for(float side:{-1.f,1.f})check(structure(vec3(side*.193134832f,-.557051054f,-.431092489f),false)>.010f,"Specter lower saddle rim clears full-rudder shin witness");
  check(structure(vec3(.2f,-.520f,-.44f),false)<-.010f,"Specter upper saddle support remains solid");
  for(float side:{-1.f,1.f}){
    check(footwellLight(vec3(side*.159208171f,-.599872070f,-.419991894f))>.20f,"recessed footwell strip clears former shin/crossbar witness");
    check(footwellLight(vec3(side*.195f,-.83031036f,-.516947934f))>.080f,"footwell strip clears full-rudder shoe witness");
    check(footwellLight(vec3(side*.292f,-.83f,-.625f))<0.f,"footwell strip embeds in sidewall beyond x=.29");
    check(footwellLight(vec3(side*.280f,-.83f,-.625f))<0.f,"footwell strip retains visible lens inside x=.29");
  }
  for(bool wr:{false,true}){
    std::set<int> pages;int count=wr?3:5;
    for(int i=0;i<count;++i){
      ResearchPanel p=wr?wraithPanel(i):specterPanel(i);pages.insert(p.page);
      check(std::abs(length(p.n)-1.f)<1e-5f,"unit front normal");
      check(dot(p.n,-p.c)>.1f,"screen faces eye");
      for(int x=-25;x<=25;++x)for(int y=-25;y<=25;++y){
        vec3 l(p.h.x*x/25.f,p.h.y*y/25.f,0);if(researchPanelShape(vec2(l.x,l.y),p)>-.0001f)continue;
        vec3 q=point(p,l),back=researchPanelFrame(q,p);
        check(length(back-l)<1e-5f,"glass coordinate round trip");
        check((wr?wraithPanelIndex(q):specterPanelIndex(q))==i,"analytic material mask owns entire page");
        if(!wr)check(!specterWindowCut(q),"Specter glass is never removed by direct-window cut");
        else check(!wraithFrontCut(q)&&!wraithChinCut(q),"Wraith glass is never removed by front/chin direct-window cuts");
        for(int j=0;j<count;++j)if(j!=i){
          float d=panelBody(q,wr?wraithPanel(j):specterPanel(j),wr);minFaceGap=min(minFaceGap,d);
          check(d>.005f,"glass clear of other module bodies");
        }
        // No other new body crosses the glass-to-eye ray.
        for(int ray=1;ray<=12;++ray){vec3 probe=q*(1.f-ray/13.f);check(structure(probe,wr)>-.001f,"new structure leaves primary viewing rays clear");}
      }
    }
    check(pages.count(3)==1&&pages.count(2)==1&&pages.count(0)==1,"flight, map, engine retained");
    if(!wr)check(pages==std::set<int>({0,1,2,3,4}),"all five Specter arc pages retained once");
    // Conservative boxes contain the complete moving grip/stem shape. Every box corner and edge/interior point
    // is sampled over combined pitch/roll and full throttle travel; intentional original base joints are excluded.
    float side=wr?.5f:.42f,baseY=wr?-.41f:-.375f,baseZ=wr?-.06f:-.02f;
    for(int pitch=-1;pitch<=1;++pitch)for(int roll=-1;roll<=1;++roll)for(int x=-1;x<=1;++x)for(int y=0;y<=6;++y)for(int z=-1;z<=1;++z){
      vec3 l(x*.03f,y*.035f,-.01f+z*.04f);vec3 q=vec3(side,baseY,baseZ)+rotateStick(l,float(pitch),float(roll));
      float d=structure(q,wr);minControlGap=min(minControlGap,d);check(d>.025f,"combined pitch/roll stick envelope clears new structure");
    }
    for(int t=0;t<=10;++t)for(int x=-1;x<=1;++x)for(int y=-1;y<=1;++y)for(int z=-1;z<=1;++z){
      float travel=wr?.13f:.12f;vec3 q(-side,wr?-.38f:-.34f,(wr?-.08f:-.02f)+travel*(.5f-t*.1f));
      q=q+vec3(x*.033f,y*.046f,z*.061f);float d=structure(q,wr);minControlGap=min(minControlGap,d);check(d>.025f,"full throttle envelope clears new structure");
    }
    for(int yaw=-1;yaw<=1;++yaw)for(int sideSign:{-1,1})for(int x=-1;x<=1;++x)for(int y=-1;y<=1;++y)for(int z=-1;z<=1;++z){
      vec3 q(sideSign*(wr?.16f:.145f)+x*.051f,(wr?-.675f:-.820f)+y*.076f,(wr?-.78f:-.73f)-sideSign*yaw*(wr?.04f:.055f)+z*.013f);
      float d=structure(q,wr);minControlGap=min(minControlGap,d);check(d>.025f,"pedal travel clears new primary bridge");
    }
  }
  // The old 12-step sightline test skipped the first ~77 mm in front of each display. The bridge
  // actually penetrated the side glass by 13.7 mm there. Sample the face and the first 4% of every
  // ray densely (about 1 mm steps), requiring at least 15 mm of analytic clearance.
  float minBridgeGap=1e6f;
  for(int i:{0,2}){
    ResearchPanel p=wraithPanel(i);
    for(int x=-60;x<=60;++x)for(int y=-36;y<=36;++y){
      vec3 local(p.h.x*x/60.f,p.h.y*y/36.f,0);
      if(researchPanelShape(vec2(local.x,local.y),p)>-.0001f)continue;
      vec3 q=point(p,local);
      for(int ray=0;ray<=40;++ray){
        float gap=wraithBridge(q*(1.f-ray*.001f));minBridgeGap=min(minBridgeGap,gap);
        check(gap>.015f,"recessed Wraith bridge clears adjacent glass and near-face pilot rays");
      }
    }
    // The recessed bridge still meets the solid rear of both side modules, rather than leaving
    // their brackets floating. This point is 52 mm behind and 60 mm below each glass centre.
    vec3 joint=point(p,vec3(0,-.060f,-.052f));
    check(wraithBridge(joint)<-.003f&&panelBody(joint,p,true)<-.003f,"Wraith side-module rear remains joined to bridge");
  }
  check(wraithBridge(point(wraithPanel(1),vec3(0,-.090f,-.053f)))<-.003f,
        "Wraith centre-module rear remains joined to bridge");
  std::printf("Wraith adjacent-screen near-face bridge clearance: %.1f mm\n",minBridgeGap*1000);
  check(wraithPanel(1).h.x*wraithPanel(1).h.y>2*wraithPanel(0).h.x*wraithPanel(0).h.y,"Wraith primary area hierarchy");
  check(specterPanel(2).h.x*specterPanel(2).h.y>1.8f*specterPanel(1).h.x*specterPanel(1).h.y,"Specter primary area hierarchy");
  check(std::abs(WR_HOLO_CENTRE.x)-WR_HOLO_RADIUS>.6f,"hologram outside central flight scan");
  // Front floor/bomb pane remains fully visible below the new instrument bridge. Pedal/rib coverage is unchanged.
  for(int x=-15;x<=15;++x)for(int z=-15;z<=15;++z){
    vec3 q(x*.4f/15.f,-.775f,-.66f+z*.34f/15.f);
    if(std::abs(q.x)+std::abs(q.z+.66f)>.64f)continue;
    for(int t=1;t<=32;++t)check(structure(q*(t/33.f),true)>-.001f,"new bridge leaves the floor/bomb pane unobstructed");
  }
  std::printf("%s: %d checks; minimum inter-module glass clearance %.1f mm, moving-control clearance %.1f mm\n",failures?"FAIL":"PASS",checks,minFaceGap*1000,minControlGap*1000);
  return failures?1:0;
}
