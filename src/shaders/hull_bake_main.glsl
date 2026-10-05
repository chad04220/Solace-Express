//! kHullBakeMain
//! Quarter-resolution cloud pass: each texel marches the clouds along the camera ray of one of the four full-resolution
//! pixels it covers (rotating every frame), up to that pixel's scene depth; the depth goes out too, for the upsampling
//! Aircraft hull bake (aircraft_hull.cpp): the airframe's distance at each listed point (aircraft space), the least
//! over every listed state of the gear, flaps, steering and controls - so a hull built from it holds the airframe in
//! any of them

uniform sampler2D uHPts; uniform int uHStN; uniform vec4 uHStPS[128]; uniform vec4 uHStCtl[128];
void main(){
  vec3 p = texelFetch(uHPts, ivec2(gl_FragCoord.xy), 0).xyz;
  loadMain(); pieceXf(-1);
  float d = 1e9;
  for (int s = 0; s < 128; s++) {
    if (s >= uHStN) break;
    gPS = uHStPS[s]; gCtl = uHStCtl[s];
    if (gPS.w > 0.5) loadCabinFit();
    d = min(d, mapPlane(p).x);
  }
  oColor = vec4(d); oDepth = d; oCloudMask = 0.0;
}
