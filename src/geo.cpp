#include "geo.h"
#include "planet.h"
#include <limits>
#include <stdexcept>

namespace geo {
namespace {
// Bit inspection survives Windows /fp:fast and the fast-math regression target.
bool finite(double x) {
  static_assert(sizeof(double) == sizeof(uint64_t) && std::numeric_limits<double>::is_iec559, "IEEE double required");
  uint64_t bits; std::memcpy(&bits, &x, sizeof(bits));
  return (bits & UINT64_C(0x7ff0000000000000)) != UINT64_C(0x7ff0000000000000);
}
void require(bool ok, const char* message) { if (!ok) throw std::invalid_argument(message); }
void check(Double3 v) { require(finite(v.x) && finite(v.y) && finite(v.z), "non-finite vector"); }
Double3 checked(Double3 v) {
  if (!finite(v.x) || !finite(v.y) || !finite(v.z)) throw std::overflow_error("geographic vector overflow");
  return v;
}
void check(Position p) {
  require(finite(p.latitude) && finite(p.longitude) && finite(p.height), "non-finite position");
  require(p.latitude >= -90 && p.latitude <= 90 && p.longitude >= -180 && p.longitude <= 180, "invalid latitude/longitude");
  require(p.height > -radius(), "position at/below Earth's centre");
}
double clampUnit(double x) { return std::clamp(x, -1.0, 1.0); }
double canonicalLongitude(double x) { return x >= 180 ? -180 : x; }
struct Basis { Double3 n, a, b; };
const Basis bases[] = {
  {{1,0,0}, {0,1,0}, {0,0,1}}, {{-1,0,0}, {0,-1,0}, {0,0,1}},
  {{0,1,0}, {-1,0,0}, {0,0,1}}, {{0,-1,0}, {1,0,0}, {0,0,1}},
  {{0,0,1}, {0,1,0}, {-1,0,0}}, {{0,0,-1}, {0,1,0}, {1,0,0}}
};
int faceIndex(Face face) { int i = static_cast<int>(face); require(i >= 0 && i < 6, "invalid cube face"); return i; }
int levelCount(int level) { require(level >= 0 && level <= kMaxTileLevel, "invalid cube level"); return 1 << level; }
void check(Tile t) {
  faceIndex(t.face); const int n = levelCount(t.level);
  require(t.x >= 0 && t.x < n && t.y >= 0 && t.y < n, "tile index outside level");
}
double angle(Double3 a, Double3 b) { return std::atan2(length(cross(a, b)), clampUnit(dot(a, b))); }
const LocalFrame& patchFrame() { static const LocalFrame f(solaceOrigin()); return f; }
}

double radius() { return static_cast<double>(PLANET_RADIUS); }
double dot(Double3 a, Double3 b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
Double3 cross(Double3 a, Double3 b) { return {a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x}; }
double length(Double3 v) { return std::hypot(v.x, v.y, v.z); }
Double3 unit(Double3 v) {
  check(v);
  const double scale = std::max({std::abs(v.x), std::abs(v.y), std::abs(v.z)});
  require(scale > 0, "zero direction");
  v = v / scale; return v / length(v); // avoid overflow for very large finite inputs
}
vec3 toFloat(Double3 v) {
  check(v); const double m = std::numeric_limits<float>::max();
  if (std::abs(v.x) > m || std::abs(v.y) > m || std::abs(v.z) > m) throw std::overflow_error("local float overflow");
  return {static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z)};
}
Double3 direction(Position p) {
  check(p);
  if (p.latitude == 90 || p.latitude == -90) return {0, 0, p.latitude > 0 ? 1.0 : -1.0};
  const double lat = p.latitude*kDegrees, lon = canonicalLongitude(p.longitude)*kDegrees;
  Double3 d(std::cos(lat)*std::cos(lon), std::cos(lat)*std::sin(lon), std::sin(lat));
  // Match cube.py's canonical axis directions, including +180/-180 ownership.
  if (std::abs(d.x) < 1e-15) d.x = 0;
  if (std::abs(d.y) < 1e-15) d.y = 0;
  if (std::abs(d.z) < 1e-15) d.z = 0;
  return d;
}
Position fromDirection(Double3 d, double height) {
  d = unit(d);
  Position p{std::atan2(d.z, std::hypot(d.x,d.y))/kDegrees,
             canonicalLongitude(std::atan2(d.y,d.x)/kDegrees), height};
  check(p); return p;
}
Double3 toEcef(Position p) { return checked(direction(p)*(radius()+p.height)); }
Position fromEcef(Double3 p) {
  check(p); const double r = length(p);
  require(r > 0, "Earth centre has no geographic position");
  if (!finite(r)) throw std::overflow_error("ECEF radius overflow");
  return fromDirection(p, r-radius());
}
double surfaceDistance(Position a, Position b) { return radius()*angle(direction(a), direction(b)); }
std::optional<double> initialBearing(Position a, Position b) {
  const Double3 da = direction(a), db = direction(b);
  if (length(cross(da,db)) <= 1e-14) return std::nullopt;
  const Double3 tangent = LocalFrame(a).vectorFromEcef(db);
  double degrees = std::atan2(tangent.x, -tangent.z)/kDegrees;
  if (degrees < 0) degrees += 360;
  return degrees >= 360 ? 0.0 : degrees; // tiny negative angles can round up to exactly 360
}
Position destination(Position start, double bearing, double metres) {
  check(start); require(finite(bearing) && finite(metres) && metres >= 0, "invalid destination course/distance");
  const double heading = std::fmod(bearing,360.0)*kDegrees;
  const double theta = std::fmod(metres/radius(),2*kPi);
  const Double3 tangent = LocalFrame(start).vectorToEcef({std::sin(heading),0,-std::cos(heading)});
  return fromDirection(direction(start)*std::cos(theta) + tangent*std::sin(theta), start.height);
}

DestinationFrom::DestinationFrom(Position start) : start_(start), frame_((check(start), start)), up_(direction(start)) {}
Position DestinationFrom::operator()(double bearing, double metres) const {
  require(finite(bearing) && finite(metres) && metres >= 0, "invalid destination course/distance");
  const double heading = std::fmod(bearing,360.0)*kDegrees;
  const double theta = std::fmod(metres/radius(),2*kPi);
  const Double3 tangent = frame_.vectorToEcef({std::sin(heading),0,-std::cos(heading)});
  return fromDirection(up_*std::cos(theta) + tangent*std::sin(theta), start_.height);
}

LocalFrame::LocalFrame(Position origin) : origin_(origin) {
  check(origin); origin_.longitude = canonicalLongitude(origin.longitude);
  const double lon = origin_.longitude*kDegrees;
  ecef_ = toEcef(origin_); up_ = direction(origin_);
  east_ = {-std::sin(lon),std::cos(lon),0};
  south_ = cross(east_, up_); // east x up = south
}
Double3 LocalFrame::vectorToEcef(Double3 v) const { check(v); return checked(east_*v.x+up_*v.y+south_*v.z); }
Double3 LocalFrame::vectorFromEcef(Double3 v) const { check(v); return checked({dot(east_,v),dot(up_,v),dot(south_,v)}); }
Double3 LocalFrame::pointToEcef(Double3 p) const { return checked(ecef_+vectorToEcef(p)); }
Double3 LocalFrame::pointFromEcef(Double3 p) const { check(p); return vectorFromEcef(checked(p-ecef_)); }
Double3 LocalFrame::toLocalDouble(Position p) const { return pointFromEcef(toEcef(p)); }
vec3 LocalFrame::toLocal(Position p) const { return toFloat(toLocalDouble(p)); }
Position LocalFrame::toPosition(Double3 p) const { return fromEcef(pointToEcef(p)); }

FrameChange::FrameChange(const LocalFrame& from, const LocalFrame& to) {
  translation_ = to.pointFromEcef(from.ecefOrigin());
  x_ = to.vectorFromEcef(from.vectorToEcef({1,0,0}));
  y_ = to.vectorFromEcef(from.vectorToEcef({0,1,0}));
  z_ = to.vectorFromEcef(from.vectorToEcef({0,0,1}));
}
Double3 FrameChange::point(Double3 p) const { return checked(translation_+vector(p)); }
Double3 FrameChange::vector(Double3 v) const { check(v); return checked(x_*v.x+y_*v.y+z_*v.z); }
quat FrameChange::attitude(quat q) const {
  require(finite(q.w)&&finite(q.x)&&finite(q.y)&&finite(q.z), "non-finite attitude");
  // Unit quaternions are the public contract; normalise a finite nonzero input.
  const double qn = std::hypot(std::hypot(double(q.w),double(q.x)),std::hypot(double(q.y),double(q.z)));
  require(qn > 0, "zero attitude");
  q = {float(q.w/qn),float(q.x/qn),float(q.y/qn),float(q.z/qn)};
  double w,x,y,z;
  const double trace = x_.x+y_.y+z_.z;
  if (trace > 0) {
    const double s = 2*std::sqrt(trace+1); w = s/4; x = (y_.z-z_.y)/s; y = (z_.x-x_.z)/s; z = (x_.y-y_.x)/s;
  } else if (x_.x > y_.y && x_.x > z_.z) {
    const double s = 2*std::sqrt(1+x_.x-y_.y-z_.z); w = (y_.z-z_.y)/s; x = s/4; y = (y_.x+x_.y)/s; z = (z_.x+x_.z)/s;
  } else if (y_.y > z_.z) {
    const double s = 2*std::sqrt(1+y_.y-x_.x-z_.z); w = (z_.x-x_.z)/s; x = (y_.x+x_.y)/s; y = s/4; z = (z_.y+y_.z)/s;
  } else {
    const double s = 2*std::sqrt(1+z_.z-x_.x-y_.y); w = (x_.y-y_.x)/s; x = (z_.x+x_.z)/s; y = (z_.y+y_.z)/s; z = s/4;
  }
  quat result = quat(float(w),float(x),float(y),float(z))*q; result.normalize(); return result;
}
FloatingOrigin::FloatingOrigin(Position p, double threshold) : frame_(p), threshold_(threshold) {
  require(finite(threshold) && threshold > 0, "invalid rebase threshold");
}
bool FloatingOrigin::needsRebase(vec3 focus) const { check(Double3(focus)); return length(Double3(focus)) > threshold_; }
std::optional<FrameChange> FloatingOrigin::rebase(vec3 focus) {
  if (!needsRebase(focus)) return std::nullopt;
  LocalFrame next(frame_.toPosition(focus)); FrameChange change(frame_, next); frame_ = next; return change;
}

Position solaceOrigin() { return {kSolaceLatitude,kSolaceLongitude,0}; }
Position fromSolace(Double3 patch) {
  check(patch); const double distance = std::hypot(patch.x,patch.z);
  require(kPi-distance/radius() > 1e-12, "Solace chart reaches its antipode");
  if (distance == 0) { Position p = solaceOrigin(); p.height = patch.y; check(p); return p; }
  const double theta = distance/radius(), rotation = kSolaceNorthRotation*kDegrees;
  const double east = patch.x*std::cos(rotation)-patch.z*std::sin(rotation);
  const double south = patch.x*std::sin(rotation)+patch.z*std::cos(rotation);
  Double3 d = direction(solaceOrigin())*std::cos(theta) +
              patchFrame().vectorToEcef({east/distance,0,south/distance})*std::sin(theta);
  return fromDirection(d,patch.y);
}
Position fromSolace(vec3 patch) { return fromSolace(Double3(patch)); }
Double3 toSolace(Position p) {
  const Double3 d = direction(p), c = direction(solaceOrigin());
  const double theta = angle(c,d);
  require(kPi-theta > 1e-12, "Solace antipode has no unique chart coordinate");
  const Double3 tangent = patchFrame().vectorFromEcef(d);
  const double norm = std::hypot(tangent.x,tangent.z);
  if (norm < 1e-15) return {0,p.height,0};
  const double scale = radius()*theta/norm, rotation = kSolaceNorthRotation*kDegrees;
  return {(tangent.x*std::cos(rotation)+tangent.z*std::sin(rotation))*scale,p.height,
          (-tangent.x*std::sin(rotation)+tangent.z*std::cos(rotation))*scale};
}

Double3 faceDirection(Face face, double u, double v) {
  const Basis& b = bases[faceIndex(face)];
  require(finite(u)&&finite(v)&&u>=-1&&u<=1&&v>=-1&&v<=1,"invalid face coordinate");
  return unit(b.n+b.a*std::tan(u*kPi/4)+b.b*std::tan(v*kPi/4));
}
FaceUV directionFace(Double3 d) {
  d = unit(d); int face = 0; double best = dot(d,bases[0].n);
  for (int i=1;i<6;++i) { double score=dot(d,bases[i].n); if(score>best) {face=i;best=score;} }
  const Basis& b = bases[face];
  return {static_cast<Face>(face),clampUnit(std::atan2(dot(d,b.a),best)*4/kPi),clampUnit(std::atan2(dot(d,b.b),best)*4/kPi)};
}
Double3 tileDirection(Tile t, double u, double v) {
  check(t); require(finite(u)&&finite(v)&&u>=0&&u<=1&&v>=0&&v<=1,"invalid tile coordinate");
  const int n=levelCount(t.level); return faceDirection(t.face,2*(t.x+u)/n-1,2*(t.y+v)/n-1);
}
TileUV directionTile(Double3 d, int level) {
  const int n=levelCount(level); const FaceUV f=directionFace(d);
  const double x=(f.u+1)*n/2,y=(f.v+1)*n/2;
  const int ix=std::min(n-1,int(x)),iy=std::min(n-1,int(y));
  return {{f.face,level,ix,iy},x-ix,y-iy};
}
TileUV positionTile(Position p,int level) { return directionTile(direction(p),level); }
SampleAddress sampleAddress(Tile t,int column,int row) {
  check(t); require(column>=-1&&column<=kTileSamples&&row>=-1&&row<=kTileSamples,"sample outside one-cell apron");
  const int count=kTileSamples*levelCount(t.level),i=t.x*kTileSamples+column,j=t.y*kTileSamples+row;
  const bool oi=i<0||i>=count,oj=j<0||j>=count;
  if(!oi&&!oj)return {t.face,i,j};
  const Basis& b=bases[faceIndex(t.face)]; const int si=i<0?-1:1,sj=j<0?-1:1;
  int face=0;
  if(oi&&oj) {
    const Double3 corner=b.n+b.a*si+b.b*sj;
    for(;face<6;++face)if(dot(corner,bases[face].n)==1)break;
    return {static_cast<Face>(face),dot(corner,bases[face].a)>0?count-1:0,dot(corner,bases[face].b)>0?count-1:0};
  }
  const Double3 next=oi?b.a*si:b.b*sj;
  for(;face<6;++face)if(dot(next,bases[face].n)==1)break;
  const Double3 variable=oi?b.b:b.a; const int index=oi?j:i;
  auto coord=[&](Double3 axis) {const double fixed=dot(b.n,axis);return fixed!=0?(fixed>0?count-1:0):(dot(variable,axis)>0?index:count-1-index);};
  return {static_cast<Face>(face),coord(bases[face].a),coord(bases[face].b)};
}
Double3 sampleDirection(Tile t,int column,int row) {
  const SampleAddress a=sampleAddress(t,column,row); const int count=kTileSamples*levelCount(t.level);
  return faceDirection(a.face,2*(a.column+.5)/count-1,2*(a.row+.5)/count-1);
}
vec3 tileLocal(Tile t,double u,double v,double height,const LocalFrame& frame) {return frame.toLocal(fromDirection(tileDirection(t,u,v),height));}
TileUV localTile(vec3 local,int level,const LocalFrame& frame) {return positionTile(frame.toPosition(local),level);}
} // namespace geo
