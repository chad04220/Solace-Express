//! kNoiseTex
//! The baked noise textures: the tileable cloud coverage map (fbm2 at 4 octaves) and the 3D value noise volume, with their lookups.
uniform sampler2D uCloudCov; uniform sampler3D uNoise3;
float cn3(vec3 x){ return textureLod(uNoise3, x*(1.0/32.0), 0.0).r; }   // vnoise3 from the baked volume
float tfbm(vec2 x){ return textureLod(uCloudCov, x*(1.0/16.0), 0.0).r*0.9375; }   // fbm2(x, 4) from the baked map
