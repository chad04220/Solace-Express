// Spherical coordinates, canonical globe addresses and an opt-in rigid local frame.
// Procedural flights use the surface charts in region_travel.h, where y is altitude.
// The rigid ECEF frame below is intentionally distinct from that live flight model.
#pragma once
#include "common.h"
#include <optional>

namespace geo {
constexpr double kPi = 3.141592653589793238462643383279502884;
constexpr double kDegrees = kPi / 180.0;
constexpr double kRebaseDistance = 20000.0;
constexpr int kMaxTileLevel = 16;  // tools/earth/cube.py's on-disk contract
constexpr int kTileSamples = 256;

// ECEF uses X at longitude 0, Y at 90 E, Z north, exactly as the offline pack.
struct Double3 {
  double x = 0, y = 0, z = 0;
  Double3() = default;
  Double3(double a, double b, double c) : x(a), y(b), z(c) {}
  explicit Double3(vec3 v) : x(v.x), y(v.y), z(v.z) {}
  Double3 operator+(Double3 b) const { return {x + b.x, y + b.y, z + b.z}; }
  Double3 operator-(Double3 b) const { return {x - b.x, y - b.y, z - b.z}; }
  Double3 operator*(double s) const { return {x * s, y * s, z * s}; }
  Double3 operator/(double s) const { return {x / s, y / s, z / s}; }
};
double dot(Double3 a, Double3 b);
Double3 cross(Double3 a, Double3 b);
double length(Double3 v);
Double3 unit(Double3 v);
vec3 toFloat(Double3 v);  // checked narrowing; floats are for nearby objects only

struct Position {
  double latitude = 0;   // degrees [-90,90]
  double longitude = 0;  // degrees [-180,180], output canonicalised to [-180,180)
  double height = 0;     // radial metres above PLANET_RADIUS; strictly > -radius
};
// Invalid/non-finite inputs throw std::invalid_argument; unrepresentable output
// throws std::overflow_error. Zero vectors, Earth's centre and invalid tiles are
// errors. No environment-dependent planetRadius() scaling enters this subsystem.
double radius();
Double3 direction(Position p);
Position fromDirection(Double3 direction, double height = 0);
Double3 toEcef(Position p);
Position fromEcef(Double3 p);
double surfaceDistance(Position a, Position b);  // shortest great-circle metres, ignores heights
std::optional<double> initialBearing(Position a, Position b); // [0,360) clockwise from true north; none at coincidence/antipodes
Position destination(Position start, double bearingDegrees, double distanceMetres); // retains start height

// A rigid Cartesian frame: +x east, +y up, +z south, right handed. Its origin
// includes height, so vertical/orbital flight can rebase too. Local y is Cartesian
// displacement, NOT altitude: get altitude from toPosition(). At poles the supplied
// origin longitude defines the frame's otherwise arbitrary east/north meridian.
class LocalFrame {
public:
  explicit LocalFrame(Position origin = {});
  Position origin() const { return origin_; }
  Double3 ecefOrigin() const { return ecef_; }
  Double3 pointToEcef(Double3 local) const;
  Double3 pointFromEcef(Double3 ecef) const;
  Double3 vectorToEcef(Double3 local) const;
  Double3 vectorFromEcef(Double3 ecef) const;
  Double3 toLocalDouble(Position p) const;
  vec3 toLocal(Position p) const;
  Position toPosition(Double3 local) const;
  Position toPosition(vec3 local) const { return toPosition(Double3(local)); }
private:
  Position origin_;
  Double3 ecef_, east_, up_, south_;
};

// Generalises a seam translation to a rigid frame change. Transform every local
// POINT (including camera/history/effects/route targets), rotate every world-frame
// VECTOR, and left-multiply every local attitude. Body-space values do not change.
// Persistent geography stays in double Position; do not repeatedly transform it
// through floats. This is a value object so consumers can apply it atomically.
class FrameChange {
public:
  FrameChange(const LocalFrame& from, const LocalFrame& to);
  Double3 point(Double3 p) const;
  vec3 point(vec3 p) const { return toFloat(point(Double3(p))); }
  Double3 vector(Double3 v) const;
  vec3 vector(vec3 v) const { return toFloat(vector(Double3(v))); }
  quat attitude(quat q) const;
private:
  Double3 translation_, x_, y_, z_;
};

class FloatingOrigin {
public:
  explicit FloatingOrigin(Position origin = {}, double threshold = kRebaseDistance);
  const LocalFrame& frame() const { return frame_; }
  double threshold() const { return threshold_; }
  bool needsRebase(vec3 focus) const; // strict > threshold, 3D Euclidean distance
  // Returns no change at/below threshold. Above it, resets origin to focus's
  // double geographic position and returns the old->new transform. Caller applies
  // it once to ALL transient local state before the next physics/render update.
  std::optional<FrameChange> rebase(vec3 focus);
private:
  LocalFrame frame_;
  double threshold_;
};

// Fixed Solace chart. x/z are exactly the current flat-world metres; y is height
// above the sphere. This azimuthal-equidistant chart is distinct from LocalFrame.
// Its horizontal angular radius must be more than 1e-12 radians short of pi
// (the antipode has no unique chart, and its immediate vicinity is ill conditioned).
constexpr double kSolaceLatitude = 32.5;
constexpr double kSolaceLongitude = -58.0;
constexpr double kSolaceNorthRotation = 0.0; // clockwise degrees: -z true north, +x east
Position solaceOrigin();
Position fromSolace(Double3 patch);
Position fromSolace(vec3 patch);
Double3 toSolace(Position p);

// Tangent-adjusted cube map. Face order and exact-edge tie priority MUST match
// tools/earth/cube.py: px,nx,py,ny,pz,nz. Tile u/v run [0,1]; samples are centred.
enum class Face { PX, NX, PY, NY, PZ, NZ };
struct FaceUV { Face face; double u, v; }; // full-face coordinates [-1,1]
struct Tile { Face face; int level, x, y; };
struct TileUV { Tile tile; double u, v; };
struct SampleAddress { Face face; int column, row; }; // global face sample indices
Double3 faceDirection(Face face, double u, double v);
FaceUV directionFace(Double3 direction);
Double3 tileDirection(Tile tile, double u, double v);
TileUV directionTile(Double3 direction, int level);
TileUV positionTile(Position p, int level);
// Aprons are aliases of integer-owned samples, not extrapolated face UVs.
SampleAddress sampleAddress(Tile tile, int column, int row); // -1..256 inclusive
Double3 sampleDirection(Tile tile, int column, int row);
vec3 tileLocal(Tile tile, double u, double v, double height, const LocalFrame& frame);
TileUV localTile(vec3 local, int level, const LocalFrame& frame);
} // namespace geo
