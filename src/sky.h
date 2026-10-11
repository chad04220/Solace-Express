// Solace Express - the sky's bodies: the sun's path, the celestial sphere turning with it, the moon and its phase, and
// the stars from the Yale Bright Star Catalogue (star_catalog.h). The sun keeps the path it always had (sunDirectionAt);
// the stars turn about the pole that path implies - the islands' latitude, about 6 degrees north - with the sun where
// it stands among them in late January, and the moon is out at its phase on today's date. The lighting pass draws them
// (light_fs.glsl starLight, common.glsl skyColorAt's moon and sun) and lights the ground by the moon's own direction.
#pragma once
#include "common.h"

// where the sun stands at a time of day (hours): its path across the islands' sky (Game::computeSun's)
vec3 sunDirectionAt(float tod);

struct SkyBodies {
  float toEquator[9];   // the game's frame (x east, y up, z south) into the equatorial (J2000: x the equinox, z the north pole), column-major
  vec3 moonDir;         // towards the moon, in the game's frame
  float moonLit = 0;    // the share of its disc the sun lights (0 new .. 1 full)
  float moonAge = 0;    // days since new moon
};
// the sky at a time of day, the moon at its age (days since new; < 0: today's, from the clock - MOONAGE sets it)
SkyBodies skyBodiesAt(float tod, float moonAge = -1.f);
// the moon's age on today's date (days since new moon), or MOONAGE's
float moonAgeToday();
