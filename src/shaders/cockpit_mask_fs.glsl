//! kCockpitMaskFS
//! Light shafts (crepuscular rays): bright, unobstructed sky and cloud around the sun is radially blurred towards
//! the sun's position on screen, so beams fan out through gaps in clouds, between hills and around the aircraft.
//! Cockpit occlusion mask for the scenery pass: where last frame's ray-traced depth found the cabin (anything within a
//! few metres of the eye, and all of a pixel's neighbours too, so head and camera motion never uncover a gap), write
//! the nearest depth so the trees and buildings behind the cabin walls are rejected before they are shaded
#version 330 core
in vec2 vUV; uniform sampler2D uDepthTex; uniform float uNear;
void main(){
  ivec2 p = ivec2(gl_FragCoord.xy), hi = textureSize(uDepthTex, 0) - 1;
  float m = 0.0;
  for (int j = -2; j <= 2; j += 2)
    for (int i = -2; i <= 2; i += 2) m = max(m, texelFetch(uDepthTex, clamp(p + ivec2(i, j), ivec2(0), hi), 0).r);
  if (!(m < uNear)) discard;
  gl_FragDepth = 0.0;
}
