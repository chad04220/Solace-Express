#include "../src/geo.h"
#include "../src/planet.h"
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
int checks=0, fails=0;
void expect(bool ok,const char* name) { ++checks; if(!ok) {++fails;std::printf("FAIL: %s\n",name);} }
bool near(geo::Double3 a,geo::Double3 b,double eps=1e-7) {return geo::length(a-b)<=eps;}
template<class F> void rejects(F action,const char* name) {
  bool caught=false;try{action();}catch(const std::invalid_argument&){caught=true;}catch(const std::overflow_error&){caught=true;}
  expect(caught,name);
}
// Live cross-language contract probe for geo_cube_parity_test.py. Each line is a
// query; no GL/world/files needed. Deliberately independent of fixture generation.
int cubeProbe() {
  using namespace geo;
  std::cout<<std::setprecision(17);
  char op;
  while(std::cin>>op) {
    if(op=='F') {
      int f;double u,v;std::cin>>f>>u>>v;const Double3 d=faceDirection(Face(f),u,v);
      std::cout<<d.x<<' '<<d.y<<' '<<d.z<<'\n';
    } else if(op=='D') {
      double x,y,z;int l;std::cin>>x>>y>>z>>l;const auto p=directionTile({x,y,z},l);
      std::cout<<int(p.tile.face)<<' '<<p.tile.x<<' '<<p.tile.y<<' '<<p.u<<' '<<p.v<<'\n';
    } else if(op=='S') {
      int f,l,x,y,c,r;std::cin>>f>>l>>x>>y>>c>>r;const Tile t{Face(f),l,x,y};
      const auto a=sampleAddress(t,c,r);const auto d=sampleDirection(t,c,r);
      std::cout<<int(a.face)<<' '<<a.column<<' '<<a.row<<' '<<d.x<<' '<<d.y<<' '<<d.z<<'\n';
    } else if(op=='L') {
      double lat,lon;int l;std::cin>>lat>>lon>>l;const auto p=positionTile({lat,lon,0},l);
      std::cout<<int(p.tile.face)<<' '<<p.tile.x<<' '<<p.tile.y<<'\n';
    } else return 2;
    if(!std::cin)return 2;
  }
  return 0;
}
}

int main(int argc,char** argv) {
  if(argc==2&&std::string(argv[1])=="--cube-probe")return cubeProbe();
  using namespace geo;
  expect(radius()==double(PLANET_RADIUS),"fixed real-Earth radius");
  expect(near(direction({0,0,0}),{1,0,0}),"prime meridian axis");
  expect(near(direction({0,90,0}),{0,1,0}),"90 E axis");
  expect(near(direction({90,113,0}),{0,0,1}),"north pole canonical direction");
  expect(near(direction({-90,-73,0}),{0,0,-1}),"south pole canonical direction");
  expect(near(direction({0,180,0}),direction({0,-180,0}),0),"antimeridian exact alias");
  expect(fromDirection({-1,0,0}).longitude==-180,"canonical output antimeridian");
  expect(std::abs(surfaceDistance({0,0,0},{0,90,9000})-radius()*kPi/2)<1e-8,"quarter-circumference ignores altitude");
  expect(std::abs(surfaceDistance({90,0,0},{-90,0,0})-radius()*kPi)<1e-8,"antipodal distance");
  expect(!initialBearing({0,0,0},{0,0,100}),"coincident bearing is undefined");
  expect(!initialBearing({0,0,0},{0,180,0}),"antipodal bearing is undefined");
  const Position start=solaceOrigin();
  for(double heading:{0.,45.,90.,135.,180.,225.,270.,315.}) {
    const auto p=destination(start,heading,123456.789);
    const auto b=initialBearing(start,p);
    expect(std::abs(surfaceDistance(start,p)-123456.789)<1e-7,"great-circle direct/inverse distance");
    expect(b&&*b>=0&&*b<360&&std::abs(std::remainder(*b-heading,360.0))<1e-10,"great-circle direct/inverse bearing");
  }
  const Position southern{-89,-180,0};
  const auto dueNorth=initialBearing(southern,destination(southern,0,123456.789));
  expect(dueNorth&&*dueNorth>=0&&*dueNorth<360&&std::abs(std::remainder(*dueNorth,360.0))<1e-10,"north bearing never rounds to 360");
  expect(surfaceDistance({0,179.999,0},{0,-179.999,0})<223,"short route across antimeridian");
  expect(std::abs(surfaceDistance(start,destination(start,12,2*kPi*radius())))<1e-7,"full circumference destination");
  for(double latitude:{-90.,-89.999999,-70.,0.,32.5,89.999999,90.})for(double longitude:{-180.,-58.,0.,123.,180.}) {
    const Position p{latitude,longitude,12345.6789};
    expect(near(toEcef(p),toEcef(fromEcef(toEcef(p))),1e-7),"global ECEF round trip");
    const LocalFrame f(p);
    const Double3 x=f.vectorToEcef({1,0,0}),y=f.vectorToEcef({0,1,0}),z=f.vectorToEcef({0,0,1});
    expect(near(cross(x,y),z,1e-14),"EUS right handed including poles");
    expect(std::abs(dot(x,y))+std::abs(dot(y,z))+std::abs(dot(x,z))<1e-14,"EUS orthogonal");
  }
  Rng rng(89173);double maxLocal=0,maxDouble=0;
  for(Position o: {Position{32.5,-58,0},Position{89.99999,179.999,700},Position{-90,18,-500},Position{0,-180,1000000}}) {
    LocalFrame f(o);
    for(int i=0;i<1000;++i) {
      const vec3 p(rng.range(-5000,5000),rng.range(-5000,5000),rng.range(-5000,5000));
      const Position a=f.toPosition(p); const vec3 b=f.toLocal(a);
      const double error=length(Double3(p)-Double3(b));maxLocal=std::max(maxLocal,error);
      maxDouble=std::max(maxDouble,length(f.toLocalDouble(a)-Double3(p)));
      expect(error<.001,"millimetre local float round trip within 5 km axes");
      // Also begin with a geographic double point that is NOT float-aligned;
      // this includes narrowing error, rather than just recovering float inputs.
      const Double3 precise=Double3(p)+Double3(.0001234567,-.0007654321,.0004321098);
      const auto geographic=f.toPosition(precise);
      const auto restored=f.toPosition(f.toLocal(geographic));
      const double narrowingError=length(toEcef(geographic)-toEcef(restored));
      maxLocal=std::max(maxLocal,narrowingError);
      expect(narrowingError<.001,"millimetre geographic-float-geographic round trip");
    }
  }
  const LocalFrame sf(start);
  const auto north=sf.toLocal(destination(start,0,1000));
  const auto east=sf.toLocal(destination(start,90,1000));
  expect(north.z< -999.99&&std::abs(north.x)<1e-7,"true north is -z");
  expect(east.x>999.99&&std::abs(east.z)<1e-7,"east is +x");
  expect(north.y<0&&std::abs(sf.toPosition(north).height)<1e-4,"Cartesian sag is not height");
  for(int level:{0,1,6,16})for(double heading:{0.,90.,180.,270.}) {
    auto p=destination(start,heading,4321.125);p.height=765.25;
    const auto tile=positionTile(p,level);
    const vec3 local=tileLocal(tile.tile,tile.u,tile.v,p.height,sf);
    expect(near(sf.pointToEcef(Double3(local)),toEcef(p),.001),"tile-direction-local conversion precision");
    const auto back=localTile(local,level,sf);
    expect(back.tile.face==tile.tile.face&&back.tile.x==tile.tile.x&&back.tile.y==tile.tile.y,"local tile membership");
    expect(near(tileDirection(back.tile,back.u,back.v),direction(p),1e-9),"local-tile-direction inverse");
  }

  // Independent great-circle destination oracle at the four authored +/-40km
  // corners. The surrounding +/-50km wrap/apron square is checked separately.
  const double corners[][4]={{-40000,-40000,32.859003952477,-58.428242512801},
    {40000,-40000,32.859003952477,-57.571757487199},
    {-40000,40000,32.139557149770,-58.424830416761},
    {40000,40000,32.139557149770,-57.575169583239}};
  for(const auto& c:corners) {
    const auto p=fromSolace(Double3(c[0],47.25,c[1]));
    expect(std::abs(p.latitude-c[2])<1e-11&&std::abs(p.longitude-c[3])<1e-11,"authored patch corner oracle");
    expect(p.height==47.25,"patch y is geographic height");
  }
  double maxPatch=0;
  for(double x:{-50000.,-40000.,-1.,0.,1.,40000.,50000.})for(double z:{-50000.,-40000.,-1.,0.,1.,40000.,50000.}) {
    const Double3 chart(x,345.625,z);const auto p=fromSolace(chart);const Double3 q=toSolace(p);
    maxPatch=std::max(maxPatch,length(chart-q));
    expect(near(chart,q,1e-7),"patch chart preserves existing x/z and height");
    expect(std::abs(surfaceDistance(start,p)-std::hypot(x,z))<1e-7,"AE patch radial distance");
  }
  expect(near(toSolace(start),{0,0,0},0),"patch centre is exact origin");
  const auto patchLocal=sf.toLocal(fromSolace(vec3(40000,0,40000)));
  expect(patchLocal.y< -250,"patch chart and Cartesian local frame are intentionally distinct");

  FloatingOrigin floating(start);
  expect(!floating.needsRebase({20000,0,0})&&!floating.rebase({20000,0,0}),"threshold is strictly greater");
  expect(floating.needsRebase({20000.01f,0,0}),"threshold crossed");
  expect(floating.needsRebase({0,0,-20001}),"negative-z threshold crossed");
  expect(floating.needsRebase({0,20001,0}),"vertical flight can rebase");
  const vec3 focus(21000,3000,-1234),velocity(123,-4,56),wind(-10,2,8);
  const auto absolute=sf.pointToEcef(Double3(focus));
  auto change=floating.rebase(focus);
  expect(bool(change)&&length(Double3(change->point(focus)))<1e-6,"rebase puts focus at zero");
  expect(near(floating.frame().pointToEcef(Double3(change->point(focus))),absolute,1e-6),"rebase keeps physical position");
  expect(!floating.rebase(change->point(focus)),"rebase does not repeat without movement");
  expect(std::abs(length(Double3(change->vector(velocity)-change->vector(wind)))-length(Double3(velocity-wind)))<1e-5,"air-relative speed survives rebase");
  double maxRigid=0,maxAttitude=0;
  for(Position destinationOrigin:{Position{32.8,-57.8,3000},Position{-32.5,122,0},Position{90,0,0},Position{-90,180,12345},Position{0,180,0}}) {
    LocalFrame other(destinationOrigin);FrameChange forward(sf,other),backward(other,sf);
    const Double3 p(1234.56,34.5,-2345.67),v(123.4,-5,234);
    maxRigid=std::max(maxRigid,length(backward.point(forward.point(p))-p));
    expect(near(other.pointToEcef(forward.point(p)),sf.pointToEcef(p),1e-7),"rigid point ECEF invariant");
    expect(near(other.vectorToEcef(forward.vector(v)),sf.vectorToEcef(v),1e-10),"rigid velocity ECEF invariant");
    expect(near(backward.point(forward.point(p)),p,1e-7),"distant-frame double round trip");
    for(quat q:{quat(),quat::axisAngle({1,2,3},1.9f),quat::axisAngle({0,1,0},PI)}) {
      const quat a=forward.attitude(q);
      for(vec3 axis:{vec3(1,0,0),vec3(0,1,0),vec3(0,0,-1)}) {
        const double e=length(other.vectorToEcef(Double3(a.rotate(axis)))-sf.vectorToEcef(Double3(q.rotate(axis))));
        maxAttitude=std::max(maxAttitude,e);expect(e<1e-6,"attitude ECEF invariant including 180 degree branches");
      }
    }
  }
  const double nan=std::numeric_limits<double>::quiet_NaN(),inf=std::numeric_limits<double>::infinity();
  for(double bad:{nan,inf,-inf}) {
    rejects([&]{direction({bad,0,0});},"invalid latitude rejected under fast math");
    rejects([&]{direction({0,bad,0});},"invalid longitude rejected under fast math");
    rejects([&]{direction({0,0,bad});},"invalid height rejected under fast math");
    rejects([&]{unit({bad,1,0});},"invalid vector rejected under fast math");
    rejects([&]{faceDirection(Face::PX,bad,0);},"invalid cube coordinate");
    rejects([&]{FloatingOrigin(start,bad);},"invalid threshold");
    rejects([&]{destination(start,bad,1);},"invalid bearing");
    rejects([&]{destination(start,0,bad);},"invalid destination distance");
  }
  rejects([&]{fromEcef({0,0,0});},"Earth centre rejected");
  rejects([&]{direction({0,0,-radius()});},"radial centre rejected");
  rejects([&]{direction({91,0,0});},"out-of-range latitude rejected");
  rejects([&]{direction({0,181,0});},"out-of-range longitude rejected");
  rejects([&]{directionFace({0,0,0});},"zero direction rejected");
  rejects([&]{faceDirection(Face(6),0,0);},"invalid face rejected");
  rejects([&]{directionTile({1,0,0},17);},"oversize level rejected before shift");
  rejects([&]{directionTile({1,0,0},-1);},"negative level rejected before shift");
  rejects([&]{tileDirection({Face::PX,1,2,0},0,0);},"invalid tile index rejected");
  rejects([&]{sampleAddress({Face::PX,0,0,0},257,0);},"invalid apron sample rejected");
  rejects([&]{FrameChange(sf,sf).attitude({0,0,0,0});},"zero quaternion rejected");
  rejects([&]{FloatingOrigin(start,0);},"zero threshold rejected");
  rejects([&]{fromSolace(Double3(kPi*radius(),0,0));},"AE boundary rejected");
  rejects([&]{toSolace({-32.5,122,0});},"AE antipode rejected");
  rejects([&]{toFloat({double(std::numeric_limits<float>::max())*2,0,0});},"float overflow rejected");
  const auto before=floating.frame().origin();
  rejects([&]{floating.rebase({std::numeric_limits<float>::quiet_NaN(),0,0});},"nonfinite focus rejected");
  expect(near(toEcef(before),toEcef(floating.frame().origin()),0),"failed rebase is atomic");
  // Very large directions are safe to normalise without squaring overflow.
  expect(near(unit({1e200,-1e200,1e200}),unit({1,-1,1}),1e-15),"scaled direction normalization");
  std::printf("Geo: %d checks, %d failures; max local float %.9g m, double %.9g m, patch %.9g m, rigid %.9g m, attitude axis %.9g\n",checks,fails,maxLocal,maxDouble,maxPatch,maxRigid,maxAttitude);
  return fails?1:0;
}
