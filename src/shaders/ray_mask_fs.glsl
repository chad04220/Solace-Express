//! kRayMaskFS
#version 330 core
in vec2 vUV; out vec4 oColor; uniform sampler2D uScene; uniform sampler2D uDepthTex; uniform vec2 uSun; uniform float uAsp;
uniform vec2 uUVS;   // the view's part of its textures (a camera feed uses a corner of larger ones)
void main(){
  vec3 c = texture(uScene, vUV*uUVS).rgb;
  float sky = step(9e5, texture(uDepthTex, vUV*uUVS).r);
  vec2 d = (vUV - uSun)*vec2(uAsp, 1.0);
  float near = exp(-dot(d, d)*2.2);
  float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
  if (!(l >= 0.0 && l < 1e6)) { oColor = vec4(0.0); return; }
  oColor = vec4(c/(1.0 + l)*smoothstep(0.35, 1.4, l)*sky*near, 1.0);
}
