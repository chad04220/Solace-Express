// Solace Express - the weather's fields: the wind an aircraft flies through and where the clouds and the rain are.
// The wind is the mean wind's profile and veer, gusts that come through in bursts, eddies carried along by the air
// (frozen turbulence: the faster you fly, the quicker the bumps, and a gust under one wing rolls you), the lift on a
// ridge's windward face and the sink and rotors in its lee, thermals under fair-weather cumulus, the bumps inside a
// cloud and a storm cell's downdraft and outflow. The clouds are the cloud pass's own (clouds.glsl cloudDensity): the
// same baked noise, the same drift, lean and boil, so the bumps, the icing, the rain and the cloud on the windscreen
// are where the clouds are drawn. Everything here is a function of place and time only (no random state), so a
// flight replays the same.
#pragma once
#include "common.h"
#include "world.h"

namespace wxfield {

// the baked cloud noise (renderer.cpp uploads these same arrays as uCloudCov and uNoise3, noise_tex.glsl): a tileable
// 1024^2 coverage map (4 octaves of value noise, period 16 coverage units = 100 km: the map wraps there, world.h WRAP_HALF) and a 128^3 value-noise volume
// (period 32 lattice cells). Generated once, on first use, on every core.
static const int kCovN = 1024, kVolN = 128;
const uint8_t* coverageMap();
const uint8_t* noiseVolume();

float cloudThickness(const Weather& wx);   // the layer's depth (m): base to top
// the cloud's density at p, 0..1 (clouds.glsl cloudDensity: detail adds the wisps' erosion; the aircraft's wake through
// the cloud is the renderer's alone)
float cloudDensity(const Weather& wx, vec3 p, bool detail);
// the cloud column over a point: > 0 under a cloud's core, < 0 in the clear between them (the coverage term of
// cloudDensity at the base; clouds.glsl cloudColumn)
float cloudColumn(const Weather& wx, float x, float z);
// how hard it rains (or snows) at p: 0..1, under the cloud cells and carried downwind as it falls (clouds.glsl
// rainColumn); 0 above the tops and when the weather has no precipitation
float rainAt(const Weather& wx, vec3 p);

// the mean wind at a height above the ground (m/s, the way the air moves): the surface layer's profile and the veer
// aloft (the wind turns clockwise with height, 15 deg by 1000 m)
vec3 meanWind(const Weather& wx, float agl);
// the velocity the eddies and gusts drift with (the air mass's): what Plane integrates its air-mass offset with
vec3 driftWind(const Weather& wx);

// the slow parts of the wind at a place (Plane::step refreshes them every step; they change over hundreds of metres)
struct Local {
  vec3 draft;            // air motion that is not the mean wind, the gusts or the eddies (m/s): ridge lift and lee sink, thermals, a storm cell's downdraft and outflow
  float sigma = 0;       // the eddies' strength here (m/s rms, vertical component)
  float cloud = 0;       // cloud density at the point (0..1)
  float rain = 0;        // precipitation intensity at the point (0..1)
  float lift = 0;        // the terrain's share of draft.y (m/s): + ridge lift, - lee sink
  float thermal = 0;     // the thermals' share of draft.y (m/s)
};
Local local(const Weather& wx, vec3 p, float agl);

// the wind at a point at an instant. airOff: the air mass's offset since the flight began (integrated driftWind), so
// the eddies are carried by the wind; span: the airframe's, whose wing averages out the eddies smaller than itself
struct Sample {
  vec3 v;                 // air velocity (m/s, world)
  vec3 gx, gy, gz;        // the gradient of each world component of v across the airframe (1/s): the gusts' rotation
  float burst = 0;        // the gust burst under way, 0..1 (1: the reported peak)
};
Sample wind(const Weather& wx, const Local& L, vec3 p, float agl, float time, vec3 airOff, float span);

}  // namespace wxfield
