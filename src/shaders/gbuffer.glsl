//! kGBuffer
//! The G-buffer the raster passes write and the lighting pass reads. GB0: view distance, octahedral normal, class;
//! GB1: sqrt(albedo), roughness; GB2: emission (HDR), metalness; GB3: ambient occlusion, -. A prelit class (water,
//! displays) carries its finished colour in GB2 and only gets the aerial perspective.
const int GB_SKY = 0, GB_TERRAIN = 1, GB_WATER = 2, GB_ENTITY = 3, GB_FOLIAGE = 4, GB_PLANE = 5, GB_CABIN = 6, GB_POD = 7, GB_PRELIT = 8,
          GB_DEBRIS = 9, GB_UFO = 10, GB_WRECK = 11;
vec2 gbOctEnc(vec3 n){ n /= abs(n.x) + abs(n.y) + abs(n.z); return n.y >= 0.0 ? n.xz : (1.0 - abs(n.zx))*vec2(n.x >= 0.0 ? 1.0 : -1.0, n.z >= 0.0 ? 1.0 : -1.0); }
