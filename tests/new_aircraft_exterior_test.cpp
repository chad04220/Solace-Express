// Scale, packed identity and deployed/stowed envelope contracts for the two authored fleet additions.
#include "aircraft.h"
#include "models.h"
#include "exhaust.h"
#include "gear_breakup_geometry.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <cstdio>

static bool near(float a,float b,float eps=.0002f){return fabsf(a-b)<eps;}
int main(){
  for(int i=0;i<kAircraftCount;i++){
    float M[96];packModelOf(i,M);
    for(float v:M)assert(std::isfinite(v));
    const int flags=int(M[79]+.5f);
    assert(flags%2==kModels[i].deice);
    assert(flags/2==(i>=kLarkspur?i:0)); // old cache/traffic packed values stay byte-compatible
    const auto&m=kModels[i];
    for(int j=1;j<8;j++)assert(m.st[j][0]>m.st[j-1][0]);
  }
  const auto&l=kModels[kLarkspur];const auto&a=kModels[kAtlas];
  assert(near(l.st[7][0]-l.st[0][0],8.8f)&&near(l.wing[0]*2,11.6f));
  assert(near(a.st[7][0]-a.st[0][0],42.6f)&&near(a.wing[0]*2,39.8f));
  for(int i=0;i<kAircraftCount;i++)if(i!=kAtlas){
    assert(kAircraft[kAtlas].fusLen>kAircraft[i].fusLen);
    assert(kAircraft[kAtlas].span>kAircraft[i].span);
  }
  float props[2][4];assert(modelProps(l,props)==1&&kAircraft[kLarkspur].blades==3);
  Plane lp;lp.spec=&kAircraft[kLarkspur];
  assert(props[0][1]-props[0][3]+lp.gearHeight()>.2f);
  assert(modelProps(a,props)==0&&kAircraft[kAtlas].engines==2&&!hasReheat(kAircraft[kAtlas]));
  Plane ap;ap.spec=&kAircraft[kAtlas];const float gh=ap.gearHeight();
  const auto gs=gearStations(*ap.spec);
  assert(near(gs.track,3.45f));
  assert(a.nacY-a.nacR+gh>.45f); // ingestion/ground-strike margin at the real physics gear height
  assert(a.nacX-a.nacR>gs.track+.34f+.145f+.7f);
  assert(a.nacY+a.nacR<a.wing[4]+a.nacX*tanf(a.wing[6]*DEG)+.1f);
  assert(near(kAircraft[kAtlas].wingY*kAircraft[kAtlas].fusRad,a.wing[4]));
  assert(near(kAircraft[kAtlas].wingZ,a.wing[5]));

  // The dedicated source-bound atlas_gear_geometry test covers the complete
  // cavity/door/skin sweep. Keep the fleet scale test on the production CPU
  // pose rather than retaining a second, obsolete copy of the fold equations.
  const auto deployed=gearBreakup::mainPose(*ap.spec,a,gs,gh,1.f);
  const auto stowed=gearBreakup::mainPose(*ap.spec,a,gs,gh,0.f);
  const vec3 wheel(gs.track,a.wheelR-gh,gs.mainZ);
  assert(length(deployed.hinge+deployed.rotation.rotate(wheel-deployed.hinge)-wheel)<.0002f);
  const vec3 end=stowed.hinge+stowed.rotation.rotate(wheel-stowed.hinge);
  assert(near(end.x,1.85f)&&near(end.y,-1.30f));
  assert(end.z>1.f&&end.z<1.15f);
  assert(near(fabsf(stowed.rotation.rotate(vec3(1,0,0)).y),1.f));
  // Root rudder thickness witness exceeded the old +/- .132m survey envelope.
  const float s0=.08f*a.vt[0],rootCh=a.vt[1]+(a.vt[2]-a.vt[1])*(s0/a.vt[0]);
  const float tt=std::max(.12f,rootCh*.11f*.21f+.003f);
  assert(tt+.03f>.145f);
  printf("new_aircraft_exterior_test: scale, fleet identity, propulsion, gear contact and swept-fold endpoints passed\n");
}
