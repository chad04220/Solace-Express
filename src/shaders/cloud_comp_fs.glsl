//! kCloudCompFS
//! Cloud composite: full resolution, blended over the ray tracer's output (colour x transmittance + in-scatter). Of the
//! four nearest cloud texels it favours those whose depth matches this pixel's, so no cloud bleeds across a silhouette.
#version 330 core
in vec2 vUV; out vec4 oColor;
uniform sampler2D uCloud; uniform sampler2D uCloudD; uniform sampler2D uDepthTex; uniform sampler2D uMaskTex;
uniform vec2 uCloudHi;   // the last cloud texel of this view
void main(){
  ivec2 p = ivec2(gl_FragCoord.xy);
  if (texelFetch(uMaskTex, p, 0).r < 0.5) discard;   // marched in the ray tracer
  float d = texelFetch(uDepthTex, p, 0).r, ld = log(max(d, 0.1));
  ivec2 hi = ivec2(uCloudHi);
  vec2 lc = (vec2(p) + 0.5)*0.5 - 0.5;
  ivec2 b = ivec2(floor(lc)); vec2 f = lc - vec2(b);
  vec4 acc = vec4(0.0); float ws = 0.0;
  for (int j = 0; j < 2; j++)
    for (int i = 0; i < 2; i++) {
      ivec2 q = clamp(b + ivec2(i, j), ivec2(0), hi);
      float wb = (i == 0 ? 1.0 - f.x : f.x)*(j == 0 ? 1.0 - f.y : f.y);
      float wd = 1.0/(0.02 + abs(ld - log(max(texelFetch(uCloudD, q, 0).r, 0.1)))*6.0);
      float w = wb*wd + 1e-6;
      acc += texelFetch(uCloud, q, 0)*w; ws += w;
    }
  oColor = acc/ws;
}
