// Source-bound Atlas gear geometry. Real ModelDef/gearStations, independently
// mirrored GLSL matrices and exact rounded wheel/leg solids; no renderer substitute.
#include "../src/aircraft.h"
#include "../src/models.h"
#include "../src/gear_breakup_geometry.h"
#include "../src/mesh_validation.h"
#include <cstdio>
#include <fstream>
#include <regex>
#include <sstream>
#include <vector>

namespace {
int failures=0,checks=0;
void check(bool ok,const char* what){++checks;if(!ok){if(failures<12)std::printf("FAIL: %s\n",what);++failures;}}
std::string source(const std::string& root,const char* path){
  std::ifstream f(root+"/"+path);std::ostringstream out;out<<f.rdbuf();check(bool(f),path);
  const std::string s=std::regex_replace(out.str(),std::regex("//[^\\n]*"),"");
  return std::regex_replace(s,std::regex("\\s+"),"");
}
void contract(const std::string& s,const char* text){check(s.find(text)!=std::string::npos,text);}
float parameter(const std::string& s,const char* expression){
  std::smatch m;const bool found=std::regex_search(s,m,std::regex(expression));check(found,expression);return found?std::stof(m[1].str()):0.f;
}
vec3 rz(vec3 p,float a){const float c=cosf(a),s=sinf(a);return vec3(c*p.x-s*p.y,s*p.x+c*p.y,p.z);}
// GLSL partRxz: positive x moves toward positive z (negative-Y quaternion).
vec3 rxz(vec3 p,float a){const float c=cosf(a),s=sinf(a);return vec3(c*p.x-s*p.z,p.y,s*p.x+c*p.z);}
float capsule(vec3 p,vec3 a,vec3 b,float r){const vec3 d=b-a;return length(p-a-d*clampf(dot(p-a,d)/dot(d,d),0,1))-r;}
float cylinder(vec3 p,float r,float h,float round){
  const float a=sqrtf(p.y*p.y+p.z*p.z)-r+round,b=fabsf(p.x)-h+round;
  return std::min(std::max(a,b),0.f)+hypotf(std::max(a,0.f),std::max(b,0.f))-round;
}
struct Cylinder{vec3 c;float r,h,round;};
struct Capsule{vec3 a,b;float r;};
vec3 support(const Cylinder& c,vec3 n){
  const float yz=hypotf(n.y,n.z);vec3 p((n.x>0?1.f:n.x<0?-1.f:0.f)*(c.h-c.round),0,0);
  if(yz>1e-8f){p.y=n.y/yz*(c.r-c.round);p.z=n.z/yz*(c.r-c.round);}
  return c.c+p+n*c.round;
}
float smin(float a,float b,float k){const float h=clampf(.5f+.5f*(b-a)/k,0,1);return lerpf(b,a,h)-k*h*(1-h);}
float box(vec3 p,vec3 h){const vec3 q(fabsf(p.x)-h.x,fabsf(p.y)-h.y,fabsf(p.z)-h.z);return length(vec3(std::max(q.x,0.f),std::max(q.y,0.f),std::max(q.z,0.f)))+std::min(std::max({q.x,q.y,q.z}),0.f);}
float wingField(const ModelDef& m,vec3 p){
  const float x=fabsf(p.x),k=clampf(x/m.wing[0],0,1),ch=lerpf(m.wing[1],m.wing[2],k);
  const float le=m.wing[5]+m.wing[3]*k,r1=m.wing[7]*ch*.5f,r2=std::max(.004f*ch,.005f),h=ch-r1-r2;
  const float b=(r1-r2)/h,a=sqrtf(1-b*b),v=p.z-le-r1,t=fabsf(p.y-m.wing[4]-x*tanf(m.wing[6]*DEG)),q=-t*b+v*a;
  return q<0?hypotf(t,v)-r1:q>a*h?hypotf(t,v-h)-r2:t*a+v*b-r1;
}
float upperSkin(const ModelDef& m,vec3 p){
  float hw,hh,cy;modelSection(m,p.z,hw,hh,cy);
  const float fus=(hypotf(p.x/hw,(p.y-cy)/hh)-1)*std::min(hw,hh);
  return -smin(fus,wingField(m,p),.08f*m.st[3][1]);
}
}
int main(int argc,char** argv){
  const std::string root=argc>1?argv[1]:".";
  const auto parts=source(root,"src/shaders/plane_parts.glsl"),field=source(root,"src/shaders/plane_sdf.glsl");
  const auto aero=source(root,"src/aero_strips.cpp"),mesh=source(root,"src/aircraft_mesh_build_fleet.h");
  // Any change to a mirrored equation fails closed. Scalar parameters are read
  // from production source so station recalibration cannot silently stale a dump.
  contract(parts,"f.H=vec3(track,-1.30,2.72);");
  contract(parts,"floatdz=-sqrt(max(v.y*v.y+v.z*v.z-dx*dx,.01));");
  contract(parts,"floatyaw=atan(v.y*dz-v.z*dx,v.y*dx+v.z*dz);");
  contract(parts,"returnpartRxz(yaw*smoothstep(0.0,.65,up))*partRxy(-1.5707963*smoothstep(.25,1.0,up));");
  contract(parts,"g.x0=-1.70;g.x1=1.70;g.hz=1.34;");
  contract(parts,"g.F.R=partRxz(atan(sweep));g.F.T=vec3(2.30,f.floor0,1.24);");
  contract(parts,"g.depth=f.H.y+.58-f.floor0;");
  contract(parts,"floatatlasMainDoorWidth(floatside){returnside<0.0?1.30:2.10;}");
  contract(parts,"floatatlasMainDoorAngle(floatside){returnsmoothstep(0.0,.2,gPS.x)*(side<0.0?1.5707963:2.50);}");
  contract(parts,"X.R=S*g.F.R*partMirror(-side)*partRxy(-atlasMainDoorAngle(side))*scale;");
  contract(parts,"return!isAtlas()&&int(gM[0].y+0.5)==4");
  contract(field,"wq=vec3(abs(q.x)-.34,q.y,abs(q.z)-.62)");
  contract(field,"tyres=sdRoundCylX(wq,wr,.145,.075)");
  contract(field,"gearWheelDetails(wq,res,wr,.145,true)");
  contract(field,"vec3face=vec3(abs(q.x)-h-0.004,q.yz);");
  contract(field,"sdRoundCylX(face,r*0.55,0.018,0.007)");
  contract(field,"sdRoundCylX(face-vec3(0.020,0.0,0.0),r*0.23,0.020,0.008)");
  contract(field,"sdRoundCylX(q+vec3(h+0.022,0.0,0.0),r*0.65,0.012,0.005)");
  contract(parts,"vec3atlasMainKnee(){returnvec3(gM[18].x,-2.40,gearHinge().z);}");
  contract(field,"legs=sdCapsule(l,mount,knee,.125);");
  contract(field,"sdCapsule(l,knee,wc+vec3(0,.10,0),.115)");
  contract(field,"sdRoundCylX(l-knee,.16,.16,.025)");
  contract(field,"sdCapsule(q,vec3(0,0,-.62),vec3(0,0,.62),.095)");
  contract(field,"sdCapsule(vec3(q.x,q.y,abs(q.z)),vec3(-.39,0,.62),vec3(.39,0,.62),.085)");
  contract(field,"sdCapsule(p,mix(upper,lower,0.86),mix(upper,lower,0.97),shaft*1.45)");
  contract(field,"sdCapsule(p,mix(upper,lower,0.12),mix(upper,lower,0.53),shaft*1.5)");
  contract(field,"vec3elbow=mix(upper,lower,0.72)+vec3(0.0,0.0,0.12*scale);");
  contract(field,"sdCapsule(p,mix(upper,lower,0.56),elbow,0.013*scale)");
  contract(field,"sdCapsule(p,elbow,mix(upper,lower,0.87),0.013*scale)");
  contract(field,"gearLegDetails(vec3(l.xy,2.0*mount.z-l.z),res,mount,knee,.095,true,false)");
  contract(field,"floatgearDoorV(vec3l,vec2h){returnsdBox(vec3(l.x-h.x*0.5,l.y+0.012,l.z),vec3(h.x*0.5,0.012,h.y-0.01));}");
  contract(field,"gearDoorV(l,vec2(2.09,gearFoldWell().hz))");
  contract(field,"res.x=min(fd,max(joined,footprint));");
  contract(field,"floatft=1.45,reach=1.75,aft=.24;");
  contract(field,"sdRoundBox(q-vec3(0,ft*.5,.5*(aft-reach)),vec3(1.94,ft*.5,halfZ+.5*(aft+reach)),.22)");
  contract(field,"floatahead=max(-q.z-halfZ-.04,0.0),curve=ft/(reach*reach);");
  contract(field,"floatramp=(curve*ahead*ahead-q.y)/sqrt(1.0+4.0*curve*curve*ahead*ahead);");
  contract(field,"boolatlasFairingOwns(floatfair,floatfootprint,floatbase){returnmax(fair,footprint)<base;}");
  contract(field,"returnmax(fair,(q.z-halfZ-.04-1.5*q.y)*.5547);");
  contract(field,"floatwell=max(box,min(res.x+0.03,w-0.04));");
  contract(field,"sdCapsule(ap,mount,f.H,.15)");
  contract(parts,"floatfixedPanel=min(p.x-(flapRoot()-.05),p.z-wingHingeZ(p.x)+.05);");
  contract(mesh,"if(type==PT_GEAR_MDOOR){lo=vec3(-.02f,-.06f,-1.36f);hi=vec3(2.12f,.03f,1.36f)");
  contract(mesh,"if(type==PT_GEAR_MAIN){lo=vec3(track-.65f,-gh-.1f,std::min(mz-1.28f,2.72f-.20f));hi=vec3(track+.65f,1.5f,mz+1.28f)");
  contract(mesh,"maxChord*thickness*.21f+.003f");
  const float targetX=parameter(parts,R"(f\.xf=([0-9.]+);)");
  const float floor=parameter(parts,R"(f\.floor0=(-[0-9.]+);)");
  const float flap=parameter(parts,R"(floatflapRoot\(\)\{returnisAtlas\(\)\?([0-9.]+):)");
  const float aeroFlap=parameter(aero,R"(flap0=idx==kAtlas\?([0-9.]+)f:)");
  check(fabsf(flap-aeroFlap)<1e-5f,"visual/aerodynamic flap roots agree");
  if(failures){std::printf("Geometry source contract changed: %d failures\n",failures);return 1;}

  const auto& spec=kAircraft[kAtlas];const auto& m=kModels[kAtlas];const auto gs=gearStations(spec);
  Plane plane;plane.spec=&spec;const float gh=plane.gearHeight(),wr=m.wheelR;
  const vec3 W(gs.track,wr-gh,gs.mainZ),H(gs.track,-1.30f,2.72f),C(2.30f,floor,1.24f),v=W-H;
  const float dx=targetX-H.x,dz=-sqrtf(v.y*v.y+v.z*v.z-dx*dx),yaw=atan2f(v.y*dz-v.z*dx,v.y*dx+v.z*dz);
  const float phi=atanf((m.wing[3]+m.wing[2]-m.wing[1])/m.wing[0]),hx=1.70f,hz=1.34f,top=H.y+.58f;
  auto rotate=[&](vec3 p,float up){return rxz(rz(p,-.5f*PI*smoothstepf(.25f,1.f,up)),yaw*smoothstepf(0,.65f,up));};
  auto inverse=[&](vec3 p,float up){return rz(rxz(p,-yaw*smoothstepf(0,.65f,up)),.5f*PI*smoothstepf(.25f,1.f,up));};
  auto pose=[&](vec3 p,float up){return H+rotate(p-H,up);};
  auto bay=[&](vec3 p){return rxz(p-C,-phi);};
  // The actual fixed blended root and its well, at the material around this bay.
  // Door solids are checked independently below. This catches the original
  // aft-shaft collision that a tyre-only cavity test could not detect.
  auto fixedRoot=[&](vec3 p){
    const vec3 b=bay(p);const float base=-upperSkin(m,p),ft=1.45f;
    const float reach=1.75f,aft=.24f,ahead=std::max(-b.z-hz-.04f,0.f),curve=ft/(reach*reach);
    float fair=box(b-vec3(0,ft*.5f,.5f*(aft-reach)),vec3(1.94f,ft*.5f,hz+.5f*(aft+reach))-vec3(.22f))-.22f;
    fair=std::max(fair,(curve*ahead*ahead-b.y)/sqrtf(1.f+4.f*curve*curve*ahead*ahead));
    fair=std::max(fair,(b.z-hz-.04f-1.5f*b.y)*.5547f);
    const float k=clampf(p.x/m.wing[0],0,1),le=m.wing[5]+m.wing[3]*k,ch=lerpf(m.wing[1],m.wing[2],k);
    const float outline=std::max({-p.x,p.x-m.wing[0],le-p.z,p.z-le-ch});
    const float footprint=std::max(outline,std::min(p.x-(flap-.05f),p.z-le-.74f*ch+.05f));
    const float res=std::min(base,std::max(smin(base,fair,.08f),footprint));
    const float depth=top-floor,cut=box(b-vec3(0,.5f*(depth-.30f),0),vec3(hx,.5f*(depth+.30f),hz));
    return std::max(res,-std::max(cut,std::min(res+.03f,b.y-.04f)));
  };
  // The .08 smooth union can add at most .02 m beyond the fairing field.
  // Its rounded-box term conservatively bounds even the new forward ramp.
  const float fairingMaxX=C.x+cosf(phi)*(1.94f+.02f)+sinf(phi)*(hz+1.75f+.02f);
  check(m.nacX-m.nacR-fairingMaxX>.08f,"leading fairing clears conservative nacelle cylinder");
  check(fabsf(W.y-wr+gh)<1e-5f,"deployed tyres match physics ground contact");
  check(aircraftMesh::kMaxPartType>=46,"fan rigid part accepted");
  check(length(pose(W,1)-vec3(targetX,H.y,H.z+dz))<1e-5f,"target derived from rigid radius equality");
  std::vector<Cylinder> wheels;
  for(float side:{-1.f,1.f})for(float axle:{-1.f,1.f}){
    const vec3 c=W+vec3(side*.34f,0,axle*.62f);wheels.push_back({c,wr,.145f,.075f});
    for(float face:{-1.f,1.f}){wheels.push_back({c+vec3(face*.149f,0,0),wr*.55f,.018f,.007f});wheels.push_back({c+vec3(face*.169f,0,0),wr*.23f,.020f,.008f});}
    wheels.push_back({c+vec3(-side*.167f,0,0),wr*.65f,.012f,.005f});
  }
  const vec3 ankle=W+vec3(0,.10f,0),knee(gs.track,-2.40f,H.z);auto along=[&](float t){return lerp(H,knee,t);};
  const vec3 elbow=along(.72f)-vec3(0,0,.18f);
  wheels.push_back({knee,.16f,.16f,.025f}); // transverse knee journal
  const std::vector<Capsule> legs={{H,knee,.125f},{knee,ankle,.115f},{W+vec3(0,0,-.62f),W+vec3(0,0,.62f),.095f},
    {along(.86f),along(.97f),.095f*1.45f},{along(.12f),along(.53f),.095f*1.5f},
    {along(.56f),elbow,.0195f},{elbow,along(.87f),.0195f},
    {W+vec3(-.39f,0,-.62f),W+vec3(.39f,0,-.62f),.085f},{W+vec3(-.39f,0,.62f),W+vec3(.39f,0,.62f),.085f}};
  std::vector<vec3> samples;
  for(int polar=0;polar<=12;polar++)for(int angle=0;angle<48;angle++){
    const float a=PI*polar/12.f,b=2*PI*angle/48.f;const vec3 n(cosf(a),sinf(a)*cosf(b),sinf(a)*sinf(b));
    for(const auto& c:wheels)samples.push_back(support(c,n));
  }
  float closedMargin=1e9f,skinMargin=1e9f,mirrorGap=1e9f,sweptWall=1e9f,maxX=-1e9f;
  for(int step=0;step<=100;step++){
    const float up=step/100.f,gear=1-up*.8f;const auto cpu=gearBreakup::mainPose(spec,m,gs,gh,gear);
    check(length(cpu.hinge-H)<1e-6f,"breakup and rendered trunnions coincide");
    for(vec3 axis:{vec3(1,0,0),vec3(0,1,0),vec3(0,0,1)})
      check(length(cpu.rotation.rotate(axis)-rotate(axis,up))<1e-5f,"CPU breakup and independent shader matrix agree");
    for(vec3 p:samples){
      const vec3 q=pose(p,up),b=bay(q);maxX=std::max(maxX,q.x);mirrorGap=std::min(mirrorGap,2*q.x);
      check(q.x>.5f,"opposite trucks cannot meet during the sweep");
      check(q.x<m.nacX-m.nacR-.1f,"moving truck clears nacelle");
      if(q.y>floor){const float margin=std::min({hx+b.x,hx-b.x,hz+b.z});sweptWall=std::min(sweptWall,margin);check(margin>.025f,"swept tyre clears inner, outer and front cavity walls above floor");}
      // Lower points are exposed while retracting. Only the upper envelope must
      // retain the existing skin; the independent optimiser covers fused root extrema.
      if(q.x>=2.05f&&q.y>m.wing[4]+q.x*tanf(m.wing[6]*DEG)){
        const float margin=upperSkin(m,q)-.03f;skinMargin=std::min(skinMargin,margin);check(margin>.04f,"wheel remains below retained upper skin");
      }
      if(step==100){const float margin=std::min({hx-fabsf(b.x),hz-fabsf(b.z),q.y-floor,top-q.y});closedMargin=std::min(closedMargin,margin);check(margin>.02f,"stowed wheel fits closed swept cavity");}
    }
  }
  // Extraction bounds are in the deployed rest frame, independent of motion.
  const vec3 extractLo(gs.track-.65f,-gh-.1f,std::min(gs.mainZ-1.28f,2.72f-.20f));
  const vec3 extractHi(gs.track+.65f,1.5f,gs.mainZ+1.28f);
  auto inExtract=[&](vec3 p){return p.x>extractLo.x&&p.x<extractHi.x&&p.y>extractLo.y&&p.y<extractHi.y&&p.z>extractLo.z&&p.z<extractHi.z;};
  for(vec3 p:samples)check(inExtract(p),"complete wheel solids fit PT33 rest extraction bounds");
  for(const auto& leg:legs)for(vec3 n:{vec3(1,0,0),vec3(-1,0,0),vec3(0,1,0),vec3(0,-1,0),vec3(0,0,1),vec3(0,0,-1)}){
    check(inExtract(leg.a+n*leg.r)&&inExtract(leg.b+n*leg.r),"complete shaft/oleo fit PT33 rest extraction bounds");
  }
  // Exact support in the six cavity normals includes all shaft/oleo/cap solids.
  for(vec3 n:{vec3(1,0,0),vec3(-1,0,0),vec3(0,1,0),vec3(0,-1,0),vec3(0,0,1),vec3(0,0,-1)}){
    const vec3 restNormal=inverse(rxz(n,phi),1);
    for(const auto& leg:legs){const vec3 end=dot(leg.a,restNormal)>dot(leg.b,restNormal)?leg.a:leg.b;
      const vec3 q=pose(end+restNormal*leg.r,1),b=bay(q);
      check(fabsf(b.x)<hx&&fabsf(b.z)<hz&&q.y>floor&&q.y<top,"stowed shaft and oleo stay within cavity");
    }
  }
  // Full fixed-field sweep, including the trailing link between its endpoints.
  // Sampling the complete primitive solids also catches intersection hidden by
  // the old, direct H-to-wheel leg. The fixed trunnion intentionally meets H.
  std::vector<vec3> assembly=samples;
  for(const auto& leg:legs)for(int alongStep=0;alongStep<=12;alongStep++)
    for(int polar=0;polar<=8;polar++)for(int angle=0;angle<24;angle++){
      const float a=PI*polar/8.f,b=2*PI*angle/24.f;
      assembly.push_back(lerp(leg.a,leg.b,alongStep/12.f)+vec3(cosf(a),sinf(a)*cosf(b),sinf(a)*sinf(b))*leg.r);
    }
  const vec3 mount(gs.track,m.wing[4]+gs.track*tanf(m.wing[6]*DEG)-.03f,H.z);
  float fixedMinimum=1e9f,trunnionGap=1e9f;
  for(int step=0;step<=200;step++)for(vec3 point:assembly){
    const vec3 p=pose(point,step/200.f);const float d=fixedRoot(p);fixedMinimum=std::min(fixedMinimum,d);
    check(d>-.0005f,"complete moving gear avoids fixed body/wing/fairing material");
    if(length(p-H)>.30f){const float gap=capsule(p,H,mount,.15f);trunnionGap=std::min(trunnionGap,gap);check(gap>.025f,"moving assembly clears fixed trunnion outside its joint");}
  }
  check(fixedRoot(vec3(3.45f,-1.552f,2.937995f))<-.03f,"negative control detects the rejected direct-shaft penetration");
  check(fixedRoot(C+rxz(vec3(1.06f,.05f,1.35f),phi))<-.005f,"closed aft lip remains solid rather than an unsealed static notch");
  float doorTruck=1e9f,doorLeg=1e9f,doorNac=1e9f,doorMirror=1e9f,doorGround=1e9f;
  for(int step=0;step<=100;step++){
    const float gear=step/100.f,up=clampf((1-gear)*1.25f,0,1);
    for(float side:{-1.f,1.f})for(int it=0;it<=32;it++)for(int iz=0;iz<=40;iz++)for(float y:{-.024f,0.f}){
      const float width=side<0?1.30f:2.10f,angle=smoothstepf(0,.2f,gear)*(side<0?.5f*PI:2.50f);
      const float t=2.09f*width/2.10f*it/32.f,z=(2.f*iz/40.f-1)*(hz-.01f),ca=cosf(angle),sa=sinf(angle);
      const vec3 q=C+rxz(vec3(side*hx-side*ca*t+side*sa*y,ca*y-sa*t,z),phi),rest=H+inverse(q-H,up);
      float tw=1e9f,tl=1e9f;for(const auto& c:wheels)tw=std::min(tw,cylinder(rest-c.c,c.r,c.h,c.round));
      for(const auto& c:legs)tl=std::min(tl,capsule(rest,c.a,c.b,c.r));
      doorTruck=std::min(doorTruck,tw);doorLeg=std::min(doorLeg,tl);
      check(tw>.04f,"door avoids all tyres, rims, caps and brakes");check(tl>.07f,"door avoids main shaft, oleo, axles and scissors");
      const float nac=hypotf(q.x-m.nacX,q.y-m.nacY)-m.nacR;doorNac=std::min(doorNac,nac);doorMirror=std::min(doorMirror,q.x);
      check(nac>.12f,"door clears conservative nacelle cylinder");check(q.x>.25f,"opposite bay doors cannot cross");
      if(step==100)doorGround=std::min(doorGround,q.y+gh);
    }
  }
  check(doorGround>.15f,"fully extended doors retain ground compression margin");
  // All closed bay corners lie under the exact wing planform. The authored
  // fairing's final union is separately clipped to the fixed-panel footprint.
  float planMargin=1e9f;
  for(float x:{-hx,hx})for(float z:{-hz,hz}){
    const vec3 p=C+rxz(vec3(x,0,z),phi);const float k=p.x/m.wing[0],le=m.wing[5]+m.wing[3]*k,ch=lerpf(m.wing[1],m.wing[2],k);
    const float margin=std::min(p.z-le,le+ch-p.z);planMargin=std::min(planMargin,margin);check(margin>.07f,"closed cavity lies fully beneath wing outline");
    check(p.x<flap-.05f||p.z<le+.74f*ch-.05f,"bay stays in fixed panel ahead of flap");
  }
  std::printf("Atlas actual contacts: track %.4f, mainZ %.5f, gearHeight %.4f; H=(%.3f,%.3f,%.3f), stowed centre=(%.3f,%.3f,%.5f), yaw %.6f\n",gs.track,gs.mainZ,gh,H.x,H.y,H.z,targetX,H.y,H.z+dz,yaw);
  std::printf("Minimum metres: closed wheel %.5f, swept wall %.5f, retained upper skin %.5f, opposite trucks %.5f, planform %.5f; maximum truck x %.5f\n",closedMargin,sweptWall,skinMargin,mirrorGap,planMargin,maxX);
  std::printf("Fixed field minimum %.6f (SDF estimator, not a physical gap); trunnion separation outside joint %.5fm; aft closure negative controls pass\n",fixedMinimum,trunnionGap);
  std::printf("Door minima metres: truck %.5f, shaft/oleo %.5f, nacelle %.5f, positive x %.5f, deployed ground %.5f; flap root %.3f\n",doorTruck,doorLeg,doorNac,doorMirror,doorGround,flap);
  std::printf("%s: %d checks, %d failures. CPU/source geometry only; cached raster and hardware validation are separate.\n",failures?"FAIL":"PASS",checks,failures);
  return failures?1:0;
}
