#pragma once
#include "common.h"
// Bounded continuous scenery-proxy sweep plus metre-spaced terrain sweep.
// Returns first fraction [0,1], or 2 when clear; accepts an unwrapped nearest-image segment.
float hiveWorldSweep(vec3 from, vec3 to, float radius);
