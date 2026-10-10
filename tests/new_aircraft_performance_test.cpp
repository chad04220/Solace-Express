// Production-aircraft engineering checks. Static cases call the same strip force / thrust model
// as Plane::step. Runway cases integrate Plane::step with a deterministic, ordinary pilot.
// No performance assertion relies on the existing autopilot being able to hold altitude.
#include "../src/aircraft.h"
#include "../src/models.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>

namespace {
int failures = 0;
void check(bool ok, const char* name, const char* requirement) {
  if (!ok) { printf("FAIL %s: %s\n", name, requirement); ++failures; }
}
struct Trim { bool ok=false; float alpha=0, elevator=0, net=0, lift=0, moment=0, stall=0; AeroIn in; AeroOut out; };
AeroOut evaluate(const AircraftSpec& s, float speed, const Atmosphere& at, float alpha,
                 float elevator, float spool, float flaps, bool gear, int failed, AeroIn* save=nullptr) {
  const AeroGeom& g = aeroGeom(s);
  AeroIn in; in.steady=true; in.va=vec3(0,-speed*sinf(alpha),-speed*cosf(alpha));
  in.rho=at.rho; in.a=at.a; in.mu=at.mu; in.pitch=elevator; in.gear=gear?1.f:0.f;
  in.flapL=in.flapR=flaps;
  Plane p; p.spec=&s; p.density=at.rho; p.soundSpeed=at.a;
  const float thrust=p.thrustAt(spool,speed,speed*cosf(alpha),at.rho)/s.engines;
  const float power=s.power*spool*powf(at.rho/1.225f,s.engineType==ENG_PISTON?1.f:.75f);
  for (int e=0;e<g.nEng;++e) {
    const bool dead=failed==-2 || failed==e;
    in.thrust[e]=dead?0.f:thrust; in.dead[e]=dead;
    if (g.eng[e].prop) { in.power[e]=dead?0.f:power; in.omega[e]=dead?0.f:s.maxRpm*.88f*2.f*PI/60.f; }
  }
  AeroMem mem; AeroOut out; aeroForces(g,s,in,mem,0.f,out);
  if(save) *save=in;
  return out;
}
Trim trim(const AircraftSpec& s,float mass,float speed,float alt,float spool,float flaps=0,bool gear=false,int failed=-1) {
  Trim t; const Atmosphere at=isa(alt); const float weight=mass*G0;
  float a=4.f*DEG,e=0;
  auto lift=[](const AeroOut& o,float a){return o.F.y*cosf(a)-o.F.z*sinf(a);};
  for(int it=0;it<45;++it) {
    AeroOut o=evaluate(s,speed,at,a,e,spool,flaps,gear,failed);
    AeroOut oa=evaluate(s,speed,at,a+.003f,e,spool,flaps,gear,failed);
    AeroOut oe=evaluate(s,speed,at,a,e+.01f,spool,flaps,gear,failed);
    const float l=lift(o,a),la=(lift(oa,a+.003f)-l)/.003f,le=(lift(oe,a)-l)/.01f;
    const float ma=(oa.M.x-o.M.x)/.003f,me=(oe.M.x-o.M.x)/.01f,det=la*me-le*ma;
    if(fabsf(det)<1e-6f)break;
    a+=clampf(((weight-l)*me+o.M.x*le)/det,-.035f,.035f);
    e+=clampf((-o.M.x*la-(weight-l)*ma)/det,-.12f,.12f);
    a=clampf(a,-10.f*DEG,24.f*DEG); e=clampf(e,-1.f,1.f);
  }
  t.out=evaluate(s,speed,at,a,e,spool,flaps,gear,failed,&t.in);
  t.alpha=a; t.elevator=e; t.lift=lift(t.out,a); t.moment=t.out.M.x;
  t.net=dot(t.out.F,normalize(t.in.va)); t.stall=t.out.stall;
  t.ok=std::isfinite(t.net)&&fabsf(t.lift-weight)<.01f*weight&&fabsf(t.moment)<.003f*weight*aeroGeom(s).MAC&&fabsf(e)<.99f;
  return t;
}
float cruise(const AircraftSpec& s,float mass,float alt,float spool) {
  float lo=std::max(s.vref*1.25f,1.3f*sqrtf(2*mass*G0/(isaDensity(alt)*s.wingArea*aeroCLmaxFlown(s,0)))),hi=isa(alt).a*.98f;
  if(!trim(s,mass,lo,alt,spool,0,!s.retract).ok)return -1;
  for(int i=0;i<28;++i){float mid=(lo+hi)*.5f;Trim t=trim(s,mass,mid,alt,spool,0,!s.retract);if(!t.ok)return -1;if(t.net>0)lo=mid;else hi=mid;}
  return (lo+hi)*.5f;
}
struct Run { bool ok=false; float dist=-1,clear15=-1,speed=0,time=0,climb=0; };
Weather calmWeather(){Weather w;w.windSpeed=0;w.gust=0;w.turbulence=0;return w;}
Run takeoff(const AircraftSpec& s,const Airport& a,float fuel,float payload) {
  Plane p;vec3 start=a.threshold(false)+a.dir()*30.f;start.y=a.elev+4.f;
  p.reset(&s,start,a.heading,fuel,payload,false);p.sceneryHits=false;p.engineRunning=true;
  const Weather wx=calmWeather();const float dt=1.f/60.f;
  // Brakes held while engine spools. A turbine departure does not start from a dead core.
  for(int k=0;k<5*60;++k){p.ctl.brake=1;p.ctl.throttle=1;p.fuel=fuel;p.step(dt,wx,k*dt);}
  const vec3 p0=p.pos;Run r;
  for(int k=0;k<120*60&&!p.ev.crashed;++k){
    p.ctl.brake=0;p.ctl.throttle=1;p.ctl.flaps=s.takeoffFlap;
    const float vr=s.vr*sqrtf(p.mass()/s.maxMass());
    p.ctl.pitch=p.ias>vr?clampf((10.f-p.pitchDeg())*.08f-p.w.x*.5f,-1.f,1.f):0;
    p.ctl.roll=clampf(-p.bankDeg()*.05f+p.w.z*.3f,-1.f,1.f);
    p.ctl.yaw=clampf(wrapAngle((a.heading-p.heading())*DEG)*3.f,-1.f,1.f);
    p.fuel=fuel;p.step(dt,wx,5.f+k*dt);
    if(!p.onGround&&p.ias>vr*.8f&&p.vel.y>.5f&&r.dist<0){r.dist=length(vec3(p.pos.x-p0.x,0,p.pos.z-p0.z));r.speed=p.ias;r.time=k*dt;}
    if(r.dist>0&&p.pos.y-a.elev>p.gearHeight()+15.f){r.ok=true;r.clear15=length(vec3(p.pos.x-p0.x,0,p.pos.z-p0.z));r.climb=p.vel.y;break;}
  }
  if(!r.ok)printf("  takeoff blocked %s %s: %s speed%.1f pitch%.1f pos%.0f\n",s.id,a.code,p.ev.crashReason.c_str(),p.ias,p.pitchDeg(),length(p.pos-p0));
  return r;
}
Run landingRoll(const AircraftSpec& s,const Airport& a,float fuel,float payload) {
  Plane p;vec3 start=a.threshold(false)+a.dir()*200.f;start.y=a.elev+4;
  p.reset(&s,start,a.heading,fuel,payload,false);p.sceneryHits=false;
  Weather wx=calmWeather();const float dt=1.f/60.f;
  for(int k=0;k<2*60;++k)p.step(dt,wx,k*dt);
  const float vs0=sqrtf(2*p.mass()*G0/(isaDensity(a.elev)*s.wingArea*aeroCLmaxFlown(s,1)));
  p.vel=p.forward()*(1.1f*vs0);p.ctl.flaps=1;p.flaps=1;p.engineRunning=true;p.engineSpool=0;
  vec3 p0=p.pos;Run r;r.speed=1.1f*vs0;
  for(int k=0;k<120*60&&!p.ev.crashed;++k){
    p.ctl.throttle=0;p.ctl.pitch=0;p.ctl.brake=1;
    p.ctl.yaw=clampf(wrapAngle((a.heading-p.heading())*DEG)*3.f,-1.f,1.f);
    p.fuel=fuel;p.step(dt,wx,2.f+k*dt);
    if(length(p.vel)<.5f){r.ok=true;r.dist=length(vec3(p.pos.x-p0.x,0,p.pos.z-p0.z));r.time=k*dt;break;}
  }
  if(!r.ok)printf("  landing blocked %s %s: %s\n",s.id,a.code,p.ev.crashReason.c_str());
  return r;
}
void approachLanding(const AircraftSpec& s,const Airport& a,float mass) {
  const float maxPayload=s.cargoKg+s.pax*85.f+85.f;
  const float fuel=std::max(s.maxFuel*.5f,mass-s.emptyMass-maxPayload),payload=mass-s.emptyMass-fuel;
  const float vs=sqrtf(2*mass*G0/(isaDensity(a.elev)*s.wingArea*aeroCLmaxFlown(s,1)));
  const float speed=1.3f*vs,gs=tanf(3.f*DEG),distance=3000.f,aim=300.f;
  float lo=0,hi=1;Trim tr;
  for(int k=0;k<22;++k){const float th=(lo+hi)*.5f;tr=trim(s,mass,speed,a.elev+100,th,1,true);if(tr.net/(mass*G0)>-sinf(3*DEG))hi=th;else lo=th;}
  const float throttle=(lo+hi)*.5f;tr=trim(s,mass,speed,a.elev+100,throttle,1,true);
  Plane p;vec3 st=a.threshold(false)-a.dir()*distance;st.y=a.elev+(s.gearHeightM>0?s.gearHeightM:s.fusRad*1.3f+.55f)+(distance+aim)*gs;
  p.reset(&s,st,a.heading,fuel,payload,true,speed);p.sceneryHits=false;
  p.q=quat::axisAngle(vec3(0,1,0),-a.heading*DEG)*quat::axisAngle(vec3(1,0,0),tr.alpha-3.f*DEG);
  p.vel=a.dir()*(speed*cosf(3.f*DEG))+vec3(0,-speed*sinf(3.f*DEG),0);
  p.gear=1;p.flaps=1;p.ctl.gearDown=true;p.ctl.flaps=1;p.engineSpool=throttle;
  Weather wx=calmWeather();float touchdown=-1,tdSink=0,yawIntegral=0;bool stopped=false;
  for(int k=0;k<180*60&&!p.ev.crashed;++k){
    const float along=dot(p.pos-a.threshold(false),a.dir()),hab=p.pos.y-a.elev-p.gearHeight();
    const float vground=dot(p.vel,a.dir()),path=std::max((aim-along)*gs,0.f);
    if(touchdown>=0){
      p.ctl.throttle=0;p.ctl.brake=1;p.ctl.pitch=clampf(-p.pitchDeg()*.025f-p.w.x*.8f,-.25f,.25f);
    }else{
      const float flare=1.f-smoothstepf(1.f,12.f,hab);
      const float sinkTarget=-(1-flare)*std::max(vground*gs,1.f)-flare*.65f;
      const float targetVS=sinkTarget+(hab>12.f?clampf((path-hab)*.25f,-3.f,3.f):0.f);
      const float targetPitch=tr.alpha/DEG+atan2f(targetVS,std::max(vground,20.f))/DEG+clampf((targetVS-p.vel.y)*.7f,-5.f,5.f);
      p.ctl.pitch=clampf(tr.elevator+(targetPitch-p.pitchDeg())*.08f-p.w.x*.8f,-1.f,1.f);
      p.ctl.throttle=clampf(throttle*(1.f-.45f*flare)+(speed*(1.f-.12f*flare)-p.airspeed)*.025f,0.f,1.f);
      p.ctl.brake=0;
    }
    const vec3 right(-a.dir().z,0,a.dir().x);
    const float cross=dot(p.pos-a.threshold(false),right),crossV=dot(p.vel,right);
    const float bankTarget=clampf(-cross*.025f-crossV*1.5f,-8.f,8.f);
    p.ctl.roll=clampf((bankTarget-p.bankDeg())*.065f+p.w.z*.4f,-1.f,1.f);
    const float headingTarget=a.heading-clampf(cross*.15f,-8.f,8.f);
    const float headingError=wrapAngle((headingTarget-p.heading())*DEG);
    yawIntegral=clampf(yawIntegral+headingError*.2f/60.f,-.8f,.8f);
    p.ctl.yaw=clampf(headingError*2.f+p.w.y*.9f+yawIntegral,-1.f,1.f);
    p.fuel=fuel;p.step(1.f/60.f,wx,k/60.f);
    if(p.ev.touchdown&&touchdown<0){touchdown=dot(p.pos-a.threshold(false),a.dir());tdSink=-p.ev.touchdownVs;}
    if(touchdown>=0&&length(p.vel)<.5f){stopped=true;break;}
  }
  const vec3 rel=p.pos-a.threshold(false);const float stop=dot(rel,a.dir()),cross=fabsf(dot(rel,vec3(-a.dir().z,0,a.dir().x)));
  printf("FINAL %s %s mass%.0f touchdown%.1f sink%.2f stop%.1f cross%.2f stopped%d crashed%d %s\n",s.id,a.code,mass,touchdown,tdSink,stop,cross,stopped,p.ev.crashed,p.ev.crashReason.c_str());
  check(stopped&&!p.ev.crashed&&touchdown>=0&&tdSink<3.f&&stop<a.length-50&&cross<a.width*.5f,s.name,"hand-flown 3-degree final, flare, touchdown and stop on runway");
}
void settledGear(const AircraftSpec& s) {
  const Airport& a=g_world.airports[g_world.findAirport("CAP")];
  const float fuel=s.maxFuel*.75f,payload=s.maxMass()-s.emptyMass-fuel;
  Plane p;vec3 st=a.threshold(false)+a.dir()*300.f;st.y=a.elev+4;
  p.reset(&s,st,a.heading,fuel,payload,false);p.sceneryHits=false;
  Weather wx=calmWeather();for(int k=0;k<10*60;++k)p.step(1.f/60.f,wx,k/60.f);
  const int idx=(int)(&s-kAircraft);const ModelDef& md=kModels[idx];
  const GearStations gs=gearStations(s);float podClear=1e9f;
  for(float side:{-1.f,1.f}){
    vec3 world=p.pos+p.q.rotate(vec3(side*md.nacX,md.nacY-md.nacR,md.nacZ0+md.nacLen*.35f));
    podClear=std::min(podClear,world.y-g_world.height(world.x,world.z,7));
  }
  printf("SETTLED %s mass%.0f originAGL%.4f pitch%.4f bank%.4f gh%.3f track%.3f mainZ%.4f nacelleClearance%.4f q[%.7f,%.7f,%.7f,%.7f]\n",s.id,p.mass(),p.agl(),p.pitchDeg(),p.bankDeg(),p.gearHeight(),gs.track,gs.mainZ,podClear,p.q.x,p.q.y,p.q.z,p.q.w);
  check(!p.ev.crashed&&p.onGround&&podClear>.30f,s.name,"loaded underwing fan clearance");
  Plane strike=p;strike.q=strike.q*quat::axisAngle(vec3(0,0,1),-5.f*DEG);strike.vel=a.dir()*6.f;strike.ctl.brake=0;
  strike.step(1.f/60.f,wx,10.f);
  printf("NACELLE_CONTACT bank5deg crashed%d reason=%s\n",strike.ev.crashed,strike.ev.crashReason.c_str());
  check(strike.ev.crashed&&strike.ev.crashReason=="Engine nacelle struck the ground",s.name,"underwing fan contact is physical at excessive ground bank");
}
// A separate deterministic hand-flown level-flight check, with no Plane autopilot. Fixed fuel
// isolates the aerodynamic handling from the intentionally compressed game fuel burn.
void levelFlight(const AircraftSpec& s,float mass,float speed,float alt=1500) {
  const float fuel=s.maxFuel*.5f; Trim tr=trim(s,mass,speed,alt,.75f,0,!s.retract);
  Plane p;p.reset(&s,vec3(-39000,alt,35000),0,fuel,mass-s.emptyMass-fuel,true,speed);
  p.q=quat::axisAngle(vec3(1,0,0),tr.alpha);p.vel=vec3(0,0,-speed);
  p.engineSpool=.75f;p.sceneryHits=false;
  const Weather wx=calmWeather();float speedSum=0,vsSum=0,bankMax=0;int n=0;
  for(int k=0;k<90*60&&!p.ev.crashed;++k){
    const float targetPitch=tr.alpha/DEG+clampf((alt-p.pos.y)*.018f-p.vel.y*.6f,-6.f,6.f);
    p.ctl.pitch=clampf(tr.elevator+(targetPitch-p.pitchDeg())*.055f-p.w.x*.8f,-1.f,1.f);
    p.ctl.roll=clampf(-p.bankDeg()*.055f+p.w.z*.4f,-1.f,1.f);
    p.ctl.yaw=clampf(wrapAngle(-p.heading()*DEG)*1.5f+p.w.y*.8f,-1.f,1.f);
    p.ctl.throttle=.75f;p.fuel=fuel;p.step(1.f/60.f,wx,k/60.f);
    if(k>60*60){speedSum+=p.airspeed;vsSum+=p.vel.y;bankMax=std::max(bankMax,fabsf(p.bankDeg()));++n;}
  }
  printf("LEVEL %s altitude%.0f target%.2f actual%.2f meanVS%+.3f altitudeError%+.1f maxBank%.2f crashed%d\n",s.id,alt,speed,speedSum/std::max(n,1),vsSum/std::max(n,1),p.pos.y-alt,bankMax,p.ev.crashed);
  check(n>0&&!p.ev.crashed&&fabsf(p.pos.y-alt)<30.f&&fabsf(speedSum/std::max(n,1)-speed)<speed*.08f,s.name,"90-second hand-flown cruise holds altitude/speed");
}
void engineOutFlight(const AircraftSpec& s) {
  const float fuel=s.maxFuel*.75f,mass=s.maxMass(),alt=1500,speed=110;
  Trim tr=trim(s,mass,speed,alt,1,0,false,0);
  Plane p;p.reset(&s,vec3(-39000,alt,35000),0,fuel,mass-s.emptyMass-fuel,true,speed);
  p.q=quat::axisAngle(vec3(1,0,0),tr.alpha);p.vel=vec3(0,0,-speed);
  p.sceneryHits=false;p.failNow(FAIL_ENGINE_TOTAL,0);p.engineSpool=.5f;
  const Weather wx=calmWeather();const float targetIas=calibratedAirspeed(speed,isa(alt));
  float yawI=0,rollI=0,maxYaw=0,vs=0,ias=0;int n=0;
  for(int k=0;k<90*60&&!p.ev.crashed;++k){
    const float yawError=wrapAngle(-p.heading()*DEG);
    yawI=clampf(yawI+yawError*.20f/60.f,-.8f,.8f);
    rollI=clampf(rollI-p.bankDeg()*.015f/60.f,-.8f,.8f);
    const float targetPitch=tr.alpha/DEG+clampf((p.ias-targetIas)*.75f,-12.f,12.f);
    p.ctl.pitch=clampf(tr.elevator+(targetPitch-p.pitchDeg())*.06f-p.w.x*.8f,-1.f,1.f);
    p.ctl.roll=clampf(-p.bankDeg()*.065f+p.w.z*.4f+rollI,-1.f,1.f);
    p.ctl.yaw=clampf(yawError*2.f+p.w.y*.9f+yawI,-1.f,1.f);
    p.ctl.throttle=1;p.fuel=fuel;p.step(1.f/60.f,wx,k/60.f);
    maxYaw=std::max(maxYaw,fabsf(wrapAngle(p.heading()*DEG)/DEG));
    if(k>60*60){vs+=p.vel.y;ias+=p.ias;++n;}
  }
  printf("ENGINE_OUT_FLIGHT %s mass%.0f 90s altitude%+.1f meanVS%+.2f meanIAS%.1f rudder%+.3f bank%+.2f maxHeadingError%.1f crashed%d\n",s.id,mass,p.pos.y-alt,vs/std::max(n,1),ias/std::max(n,1),p.ctl.yaw,p.bankDeg(),maxYaw,p.ev.crashed);
  check(n>0&&!p.ev.crashed&&fabsf(p.bankDeg())<5.f&&fabsf(wrapAngle(p.heading()*DEG)/DEG)<8.f&&fabsf(p.ctl.yaw)>.02f,s.name,"one-engine flight can be held with corrective controls");
}
float bestClimb(const AircraftSpec& s,float mass,float alt,int failed=-1) {
  float best=-1e9f;
  const float vs=sqrtf(2*mass*G0/(isaDensity(alt)*s.wingArea*aeroCLmaxFlown(s,0)));
  for(float v=1.15f*vs;v<s.cruise*1.15f;v+=s.engineType==ENG_JET?4.f:1.5f){
    Trim t=trim(s,mass,v,alt,1,0,!s.retract,failed);
    if(t.ok)best=std::max(best,t.net*v/(mass*G0));
  }
  return best;
}
} // namespace
int main(int argc,char**argv){
  const bool report=argc>1&&!strcmp(argv[1],"--report");g_world.build();
  printf("Static trim solves physical force and moment balance; TAS m/s, mass kg. Runway integrates 6-DOF.\n");
  printf("type,mass,vs_clean_SL,vs_flap_SL,vref_1.3vs,cruise75_1500m,cruise75_10000m,climb_excess_1500m\n");
  for(int ai:kCareerAircraft){
    if(!report&&ai!=kLarkspur&&ai!=kAtlas)continue;
    const AircraftSpec&s=kAircraft[ai];const bool fresh=ai==kLarkspur||ai==kAtlas;
    const float mass=s.emptyMass+.5f*s.maxFuel+.5f*(s.cargoKg+s.pax*85+85);
    const float vs1=sqrtf(2*mass*G0/(1.225f*s.wingArea*aeroCLmaxFlown(s,0)));
    const float vs0=sqrtf(2*mass*G0/(1.225f*s.wingArea*aeroCLmaxFlown(s,1)));
    const float vc=cruise(s,mass,1500,.75f),high=s.engineType==ENG_JET?cruise(s,mass,10000,.75f):0;
    const float roc=bestClimb(s,mass,1500);
    printf("%s,%.0f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f\n",s.id,mass,vs1,vs0,1.3f*vs0,vc,high,roc);
    if(!fresh)continue;
    levelFlight(s,mass,vc);
    if(ai==kAtlas)levelFlight(s,mass,high,10000);
    check(fabsf(s.wingY*s.fusRad-kModels[ai].wing[4])<.001f&&fabsf(s.wingZ-kModels[ai].wing[5])<.001f,s.name,"spec and drawn wing root agree");
    if(ai==kLarkspur)check(kModels[ai].strut==0&&fabsf(aeroModel(s).extraDq-.12e-6f*s.power)<.00001f,s.name,"cantilever wing has no phantom strut drag");
    check(aeroGeom(s).wingArea/s.wingArea>.985f&&aeroGeom(s).wingArea/s.wingArea<1.015f,s.name,"drawn wing area matches aerodynamic reference");
    check(vc>0&&vc<isa(1500).a*.9f,s.name,"trimmed subsonic cruise");
    check(roc>1.5f&&roc<(ai==kAtlas?38.f:12.f),s.name,"believable reference climb power");
    check(trim(s,mass,1.3f*vs0,0,.25f,1,true).ok,s.name,"full-flap approach trims");
    for(int load=0;load<2;++load){
      const float fuel=s.maxFuel*(load?.75f:.3f),payload=load?s.maxMass()-s.emptyMass-fuel:85.f,m=s.emptyMass+fuel+payload;
      const float stall=sqrtf(2*m*G0/(1.225f*s.wingArea*aeroCLmaxFlown(s,1)));
      const Airport&a=g_world.airports[g_world.findAirport("CAP")];
      Run to=takeoff(s,a,fuel,payload),ld=landingRoll(s,a,fuel,payload);
      printf("RUNWAY %s %s mass%.0f groundroll%.1fm lift%.1fm/s t%.1fs landroll%.1fm landing15m%.1fm approach%.1fm/s\n",s.id,load?"design-max":"light",m,to.dist,to.speed,to.time,ld.dist,ld.dist+15.f/tanf(3.f*DEG),1.3f*stall);
      printf("TAKEOFF_15M %s %s distance%.1f climb%.2f\n",s.id,load?"design-max":"light",to.clear15,to.climb);
      check(to.ok&&to.dist>50&&to.clear15<(ai==kAtlas?1900.f:650.f),s.name,"light/loaded physical takeoff");
      check(ld.ok&&ld.dist>20&&ld.dist<(ai==kAtlas?1300.f:550.f),s.name,"light/loaded physical braking");
    }
    if(ai==kAtlas){
      settledGear(s);
      const Airport& port=g_world.airports[g_world.findAirport("PVI")];
      for(int loaded=0;loaded<2;++loaded){
        const float fuel=s.maxFuel*(loaded?.75f:.5f),m=loaded?s.maxMass():mass,payload=m-s.emptyMass-fuel;
        Run take=takeoff(s,port,fuel,payload),land=landingRoll(s,port,fuel,payload);
        approachLanding(s,port,m);
        approachLanding(s,g_world.airports[g_world.findAirport("CAP")],m);
        printf("PVI %s mass%.0f takeoff15m%.1f landing15m%.1f length%.0f\n",loaded?"design-max":"mid-load",m,take.clear15,land.dist+15.f/tanf(3.f*DEG),port.length);
        check(take.ok&&land.ok&&take.clear15*1.15f<port.length&&(land.dist+15.f/tanf(3.f*DEG))*1.15f<port.length,s.name,"physical mid/max-weight Port Verde departure and braking fit unchanged runway");
      }
      engineOutFlight(s);
      const float oei=bestClimb(s,s.maxMass(),1500,0);
      Trim t=trim(s,s.maxMass(),100,1500,1,0,false,0);
      printf("ENGINE_OUT atlas loaded specific-excess%.2fm/s uncorrected-yaw-moment%.0fNm\n",oei,t.out.M.y);
      check(t.ok&&fabsf(t.out.M.y)>100000,s.name,"underwing engine failure produces real asymmetric yaw");
      check(oei>-5&&oei<bestClimb(s,s.maxMass(),1500),s.name,"one engine reduces climb capability");
      Trim aw=trim(s,mass,1.3f*vs0,0,.25f,1,true);
      AeroWake wake; aeroWakeBuild(aeroGeom(s),aw.in,aw.out,wake);
      const AircraftSpec& small=kAircraft[kLarkspur];const float sm=small.maxMass();
      const float sv=sqrtf(2*sm*G0/(1.225f*small.wingArea*aeroCLmaxFlown(small,1)))*1.3f;
      Trim sw=trim(small,sm,sv,0,.25f,1,true);AeroWake smallWake;aeroWakeBuild(aeroGeom(small),sw.in,sw.out,smallWake);
      printf("WAKE atlas circulation%.1f sink%.2f larkspur%.1f sink%.2f\n",wake.gamPair,aeroWakeSink(wake),smallWake.gamPair,aeroWakeSink(smallWake));
      check(fabsf(wake.gamPair)>fabsf(smallWake.gamPair)*3.f,s.name,"transport makes a materially stronger approach wake");
      check(s.gLimitPos()==2.5f&&s.gLimitNeg()==-1.f,s.name,"transport structural limits");
      Plane p;p.spec=&s;p.soundSpeed=340.3f;
      check(p.thrustAt(1,0,0,1.225f)<s.maxMass()*G0*.4f,s.name,"transport thrust/weight, no research reheat");
    }else{
      approachLanding(s,g_world.airports[g_world.findAirport("CAP")],s.maxMass());
      Trim t=trim(s,mass,40,0,0,0,true,-2);
      check(t.ok&&t.net<0,s.name,"stopped single cannot sustain level flight");
      check(aeroGeom(s).glideLD[1]>7&&aeroGeom(s).glideLD[1]<20,s.name,"believable windmilling glide");
    }
    const PerfModel&p=Plane::perf(&s);
    printf("LEARNED %s cruise%.1f climb%.2f vy%.1f takeoff%.1f landing15m%.1f requiredSL%.1f CAP%d PVI%d KLO%d FAR%d\n",s.id,p.cruiseV,p.roc,p.vy,p.toRoll,p.ldgRoll,s.runwayNeeded(0),runwayOK(s,g_world.airports[g_world.findAirport("CAP")]),runwayOK(s,g_world.airports[g_world.findAirport("PVI")]),runwayOK(s,g_world.airports[g_world.findAirport("KLO")]),runwayOK(s,g_world.airports[g_world.findAirport("FAR")]));
    Plane envelope;envelope.reset(&s,vec3(-39000,1500,35000),0,s.maxFuel*.75f,s.maxMass()-s.emptyMass-s.maxFuel*.75f,true,s.cruise);envelope.apSense();
    const float reference=s.emptyMass+s.maxFuel*.6f+performanceReferencePayload(s);
    check(fabsf(envelope.apEnv.wRatio-s.maxMass()/reference)<.001f,s.name,"autopilot uses learner reference mass");
    check(fabsf(envelope.apEnv.ldgDist-p.ldgRoll)<.01f,s.name,"loaded landing envelope uses true design maximum");
    check(runwayOK(s,g_world.airports[g_world.findAirport("CAP")])&&runwayOK(s,g_world.airports[g_world.findAirport("PVI")]),s.name,"usable capital/port career route");
    if(ai==kAtlas)check(!runwayOK(s,g_world.airports[g_world.findAirport("FAR")])&&!surfaceOK(s,SURF_GRASS),s.name,"large transport is excluded from short/soft fields");
  }
  printf("%s (%d failures)\n",failures?"FAILED":"PASS",failures);return failures?1:0;
}
