//! kRayFS
#version 330 core
in vec2 vUV; out vec4 oColor; uniform sampler2D uTex; uniform vec2 uSun; uniform float uJitter; uniform vec2 uUVS;
void main(){
  const int N = 56;
  vec2 dv = (uSun - vUV)/float(N)*0.92;
  vec2 p = vUV + dv*uJitter;
  vec3 acc = vec3(0.0); float decay = 1.0, wsum = 0.0;
  for (int i = 0; i < N; i++) {
    acc += texture(uTex, clamp(p, vec2(0.0), vec2(1.0))*uUVS).rgb*decay;
    wsum += decay; decay *= 0.965; p += dv;
  }
  oColor = vec4(acc/wsum*1.6, 1.0);
}
